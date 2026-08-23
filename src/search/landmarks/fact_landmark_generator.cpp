/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "mimir/search/landmarks/fact_landmark_generator.hpp"

#include "mimir/search/landmarks/fact_landmark_graph.hpp"

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/grounders/interface.hpp"
#include "mimir/search/heuristics/rpg/construction_helpers.hpp"
#include "mimir/search/openlists/priority_queue.hpp"

#include <algorithm>
#include <deque>
#include <iterator>
#include <unordered_map>

// `mimir::search::rpg` defines its own `Action`/`Axiom`, which collide with
// `mimir::formalism::Action`/`Axiom` under a blanket `using namespace`. Only import the specific
// formalism symbols used unqualified below and keep `rpg` fully open.
using mimir::formalism::DerivedTag;
using mimir::formalism::FluentTag;
using mimir::formalism::GroundAction;
using mimir::formalism::GroundActionImpl;
using mimir::formalism::NegativeTag;
using mimir::formalism::PositiveTag;
using namespace mimir::search::rpg;

namespace mimir::search::landmarks
{
namespace
{

static constexpr Index DUMMY_PROPOSITION_INDEX = 0;

struct QueueEntry
{
    using KeyType = DiscreteCost;
    using ItemType = Index;

    DiscreteCost cost;
    Index proposition_index;

    DiscreteCost get_key() const { return cost; }
    Index get_item() const { return proposition_index; }
};

/// @brief Union of a unary action's base and conditional positive fluent preconditions, sorted and deduplicated.
IndexList collect_positive_fluent_preconditions(const Action& unary_action)
{
    auto result = IndexList {};
    const auto base = unary_action.get_preconditions<PositiveTag, FluentTag>()->compressed_range();
    const auto conditional = unary_action.get_conditional_preconditions<PositiveTag, FluentTag>()->compressed_range();
    std::set_union(base.begin(), base.end(), conditional.begin(), conditional.end(), std::back_inserter(result));
    return result;
}

}

FactLandmarkGraph ApproximateFactLandmarkGenerator::create(const IGrounder& grounder, const FactLandmarkGeneratorOptions& options)
{
    const auto problem = grounder.get_problem();

    /**
     * Step 1: instantiate unary relaxed actions/axioms/atoms/propositions exactly once.
     */

    auto [actions, is_precondition_of_action, trivial_unary_actions] = instantiate_actions(grounder);
    auto [axioms, is_precondition_of_axiom, trivial_unary_axioms] = instantiate_axioms(grounder);
    auto fluent_atom_indices = instantiate_atoms<FluentTag>(*problem);
    auto derived_atom_indices = instantiate_atoms<DerivedTag>(*problem);
    auto [propositions, unused_goal_propositions, offsets] =
        instantiate_propositions(*problem, std::move(is_precondition_of_action), std::move(is_precondition_of_axiom), std::move(trivial_unary_actions), std::move(trivial_unary_axioms));
    (void) unused_goal_propositions;

    const auto num_fluent_atoms = fluent_atom_indices.size();
    const auto num_derived_atoms = derived_atom_indices.size();

    auto structures = StructuresContainer {};
    get<Action>(structures) = std::move(actions);
    get<Axiom>(structures) = std::move(axioms);

    /**
     * Step 2: full h_max-style Dijkstra fixpoint from the initial state, no goal early-exit,
     * tracking every minimal-cost ("first") achieving unary action per positive fluent proposition.
     * Axioms never achieve fluent propositions in this RPG formulation, so achiever tracking is
     * only ever populated by `Action` structures.
     */

    auto structure_annotations = StructuresAnnotationsContainer {};
    get<Action>(structure_annotations).resize(get<Action>(structures).size());
    get<Axiom>(structure_annotations).resize(get<Axiom>(structures).size());
    for (auto& annotation : get<Action>(structure_annotations))
    {
        get_cost(annotation) = 0;
    }
    for (auto& annotation : get<Axiom>(structure_annotations))
    {
        get_cost(annotation) = 0;
    }
    for (const auto& action : get<Action>(structures))
    {
        get_num_unsatisfied_preconditions(get<Action>(structure_annotations)[action.get_index()]) = action.get_num_preconditions();
    }
    for (const auto& axiom : get<Axiom>(structures))
    {
        get_num_unsatisfied_preconditions(get<Axiom>(structure_annotations)[axiom.get_index()]) = axiom.get_num_preconditions();
    }

    auto proposition_cost = std::vector<DiscreteCost>(propositions.size(), MAX_DISCRETE_COST);
    auto action_achievers_by_proposition = std::vector<IndexList>(propositions.size());

    auto queue = PriorityQueue<QueueEntry> {};

    const auto seed = [&](Index proposition_index)
    {
        proposition_cost[proposition_index] = 0;
        queue.insert(QueueEntry { 0, proposition_index });
    };

    seed(DUMMY_PROPOSITION_INDEX);

    // Fluent atoms: exact seeding from the initial state, both polarities.
    {
        auto initial_fluent_mask = FlatBitset {};
        for (const auto atom : problem->get_fluent_initial_atoms())
        {
            initial_fluent_mask.set(atom->get_index());
        }
        const auto& positive_fluent_offsets = get<PositiveTag, FluentTag>(offsets);
        const auto& negative_fluent_offsets = get<NegativeTag, FluentTag>(offsets);
        for (Index atom_index = 0; atom_index < num_fluent_atoms; ++atom_index)
        {
            seed(initial_fluent_mask.get(atom_index) ? positive_fluent_offsets[atom_index] : negative_fluent_offsets[atom_index]);
        }
    }

    // Derived atoms: only the "not-y <- T" negative propositions are seeded directly (matching the
    // same relaxation the rest of the codebase's RPG heuristics use for negated derived literals).
    // Positive derived propositions are derived purely through axiom firing below, which is exact
    // for stratified (acyclic) axiom sets since the grounder never emits genuinely cyclic axioms.
    {
        const auto& negative_derived_offsets = get<NegativeTag, DerivedTag>(offsets);
        for (Index atom_index = 0; atom_index < num_derived_atoms; ++atom_index)
        {
            seed(negative_derived_offsets[atom_index]);
        }
    }

    const auto process_fired_action = [&](const Action& action)
    {
        const auto firing_cost = get_cost(get<Action>(structure_annotations)[action.get_index()]) + 1;
        const auto& target_offsets = action.get_polarity() ? get<PositiveTag, FluentTag>(offsets) : get<NegativeTag, FluentTag>(offsets);
        const auto effect_proposition_index = target_offsets[action.get_effect()];

        if (firing_cost < proposition_cost[effect_proposition_index])
        {
            proposition_cost[effect_proposition_index] = firing_cost;
            action_achievers_by_proposition[effect_proposition_index] = IndexList { action.get_index() };
            queue.insert(QueueEntry { firing_cost, effect_proposition_index });
        }
        else if (firing_cost == proposition_cost[effect_proposition_index])
        {
            action_achievers_by_proposition[effect_proposition_index].push_back(action.get_index());
        }
    };

    const auto process_fired_axiom = [&](const Axiom& axiom)
    {
        const auto firing_cost = get_cost(get<Axiom>(structure_annotations)[axiom.get_index()]) + 1;
        const auto& target_offsets = axiom.get_polarity() ? get<PositiveTag, DerivedTag>(offsets) : get<NegativeTag, DerivedTag>(offsets);
        const auto effect_proposition_index = target_offsets[axiom.get_effect()];

        if (firing_cost < proposition_cost[effect_proposition_index])
        {
            proposition_cost[effect_proposition_index] = firing_cost;
            queue.insert(QueueEntry { firing_cost, effect_proposition_index });
        }
    };

    while (!queue.empty())
    {
        const auto entry = queue.top_entry();
        queue.pop();

        if (entry.cost > proposition_cost[entry.proposition_index])
        {
            continue;  // stale entry, superseded by an earlier improvement
        }

        const auto& proposition = propositions[entry.proposition_index];

        for (const auto action_index : proposition.is_precondition_of<Action>())
        {
            auto& annotation = get<Action>(structure_annotations)[action_index];
            get_cost(annotation) = std::max(get_cost(annotation), entry.cost);
            if (entry.proposition_index != DUMMY_PROPOSITION_INDEX)
            {
                --get_num_unsatisfied_preconditions(annotation);
            }
            if (get_num_unsatisfied_preconditions(annotation) == 0)
            {
                process_fired_action(get<Action>(structures)[action_index]);
            }
        }

        for (const auto axiom_index : proposition.is_precondition_of<Axiom>())
        {
            auto& annotation = get<Axiom>(structure_annotations)[axiom_index];
            get_cost(annotation) = std::max(get_cost(annotation), entry.cost);
            if (entry.proposition_index != DUMMY_PROPOSITION_INDEX)
            {
                --get_num_unsatisfied_preconditions(annotation);
            }
            if (get_num_unsatisfied_preconditions(annotation) == 0)
            {
                process_fired_axiom(get<Axiom>(structures)[axiom_index]);
            }
        }
    }

    /**
     * Step 3-4: backward worklist landmark discovery from positive fluent goal atoms, intersecting
     * positive fluent preconditions over the raw (pre-dedup) minimal-cost achieving unary actions.
     */

    auto landmark_atom_mask = FlatBitset {};
    auto landmark_atom_indices = IndexList {};
    auto predecessors_by_atom = std::vector<IndexList>(num_fluent_atoms);
    auto successors_by_atom = std::vector<IndexList>(num_fluent_atoms);
    auto worklist = std::deque<Index> {};

    const auto add_landmark = [&](Index atom_index)
    {
        if (!landmark_atom_mask.get(atom_index))
        {
            landmark_atom_mask.set(atom_index);
            landmark_atom_indices.push_back(atom_index);
            worklist.push_back(atom_index);
        }
    };

    if (options.include_positive_goal_facts)
    {
        for (const auto goal_atom_index : problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>())
        {
            add_landmark(goal_atom_index);
        }
    }

    const auto& positive_fluent_offsets = get<PositiveTag, FluentTag>(offsets);

    while (!worklist.empty())
    {
        const auto landmark_atom_index = worklist.front();
        worklist.pop_front();

        const auto proposition_index = positive_fluent_offsets[landmark_atom_index];
        const auto& qualifying_unary_actions = action_achievers_by_proposition[proposition_index];

        if (qualifying_unary_actions.empty())
        {
            continue;  // already true in the initial state: no predecessors
        }

        auto shared_preconditions = collect_positive_fluent_preconditions(get<Action>(structures)[qualifying_unary_actions.front()]);
        for (size_t i = 1; i < qualifying_unary_actions.size(); ++i)
        {
            const auto action_preconditions = collect_positive_fluent_preconditions(get<Action>(structures)[qualifying_unary_actions[i]]);
            auto intersected = IndexList {};
            std::set_intersection(shared_preconditions.begin(),
                                  shared_preconditions.end(),
                                  action_preconditions.begin(),
                                  action_preconditions.end(),
                                  std::back_inserter(intersected));
            shared_preconditions = std::move(intersected);
        }

        for (const auto predecessor_atom_index : shared_preconditions)
        {
            if (options.compute_greedy_necessary_orderings)
            {
                predecessors_by_atom[landmark_atom_index].push_back(predecessor_atom_index);
                successors_by_atom[predecessor_atom_index].push_back(landmark_atom_index);
            }
            add_landmark(predecessor_atom_index);
        }
    }

    /**
     * Step 4b: disjunctive landmarks, from the same minimal-cost achievers Step 3-4 intersected.
     *
     * Where that intersection asks "which atom does EVERY achiever need", this asks the weaker
     * question "which *predicate* does every achiever need", and answers with the union of the
     * atoms they contribute. Every plan fires some achiever and every achiever needs one of those
     * atoms, so the union is a landmark of the set even though no member is one individually --
     * which is exactly the information `std::set_intersection` above discards, and why a landmark
     * set can collapse onto the goal facts on a problem with several interchangeable achievers.
     *
     * Back-chaining continues through the members, because the useful stepping stones are usually
     * one level below the disjunction rather than in it: with N rovers able to photograph an
     * objective, `have_image ?r o m` is disjunctive, but each member's own preconditions are
     * single-achiever again and yield precise atoms.
     */

    auto disjunctive_landmarks = std::vector<IndexList> {};
    if (options.max_disjunctive_landmark_size > 0)
    {
        const auto predicate_index_of = [&](Index atom_index)
        { return problem->get_repositories().template get_ground_atom<FluentTag>(atom_index)->get_predicate()->get_index(); };

        auto expanded_mask = FlatBitset {};
        auto frontier = IndexList {};
        for (const auto landmark_atom_index : landmark_atom_indices)
        {
            expanded_mask.set(landmark_atom_index);
            frontier.push_back(landmark_atom_index);
        }

        // Grouped preconditions of one achiever, reused across achievers and atoms so the loop does
        // not allocate per achiever.
        auto grouped = std::unordered_map<Index, IndexList> {};
        auto by_achiever = std::vector<std::unordered_map<Index, IndexList>> {};

        for (size_t depth = 0; !frontier.empty(); ++depth)
        {
            if (options.max_disjunctive_landmark_depth > 0 && depth >= options.max_disjunctive_landmark_depth)
            {
                break;
            }

            auto next_frontier = IndexList {};
            for (const auto atom_index : frontier)
            {
                const auto& qualifying_unary_actions = action_achievers_by_proposition[positive_fluent_offsets[atom_index]];
                if (qualifying_unary_actions.empty())
                {
                    continue;  // true in the initial state: no achiever, nothing to require
                }

                by_achiever.clear();
                for (const auto unary_action_index : qualifying_unary_actions)
                {
                    grouped.clear();
                    for (const auto precondition_atom_index : collect_positive_fluent_preconditions(get<Action>(structures)[unary_action_index]))
                    {
                        grouped[predicate_index_of(precondition_atom_index)].push_back(precondition_atom_index);
                    }
                    by_achiever.push_back(grouped);
                }

                /* A predicate qualifies only if EVERY achiever contributes one of its atoms -- an
                   achiever missing it can discharge the parent without touching the union, which
                   would make the union no landmark at all. */
                for (const auto& [predicate_index, _atoms] : by_achiever.front())
                {
                    auto members = IndexList {};
                    auto in_every_achiever = true;
                    for (const auto& achiever_groups : by_achiever)
                    {
                        const auto it = achiever_groups.find(predicate_index);
                        if (it == achiever_groups.end())
                        {
                            in_every_achiever = false;
                            break;
                        }
                        members.insert(members.end(), it->second.begin(), it->second.end());
                    }
                    if (!in_every_achiever)
                    {
                        continue;
                    }

                    std::sort(members.begin(), members.end());
                    members.erase(std::unique(members.begin(), members.end()), members.end());
                    if (members.size() > options.max_disjunctive_landmark_size)
                    {
                        continue;
                    }

                    disjunctive_landmarks.push_back(members);
                    for (const auto member_atom_index : members)
                    {
                        if (!expanded_mask.get(member_atom_index))
                        {
                            expanded_mask.set(member_atom_index);
                            next_frontier.push_back(member_atom_index);
                        }
                    }
                }
            }
            frontier = std::move(next_frontier);
        }

        /* Deduplicate, and drop any set holding a fact landmark: that member is true in every plan,
           so the set is implied by it and tracking both is redundant. Done here rather than inline
           because `landmark_atom_mask` is still growing while Step 3-4 runs. */
        std::sort(disjunctive_landmarks.begin(), disjunctive_landmarks.end());
        disjunctive_landmarks.erase(std::unique(disjunctive_landmarks.begin(), disjunctive_landmarks.end()), disjunctive_landmarks.end());
        disjunctive_landmarks.erase(std::remove_if(disjunctive_landmarks.begin(),
                                                   disjunctive_landmarks.end(),
                                                   [&](const IndexList& members) {
                                                       return std::any_of(members.begin(),
                                                                          members.end(),
                                                                          [&](Index member) { return landmark_atom_mask.get(member); });
                                                   }),
                                    disjunctive_landmarks.end());
    }

    /**
     * Step 5-6: full achiever index (any RPG level) and first-achiever index (minimal-cost only),
     * both deduplicated by the underlying `GroundAction` index, computed for every fluent atom.
     */

    auto achiever_action_indices_by_atom = std::vector<IndexList>(num_fluent_atoms);
    for (const auto& unary_action : get<Action>(structures))
    {
        if (!unary_action.get_polarity())
        {
            continue;
        }
        auto& achievers = achiever_action_indices_by_atom[unary_action.get_effect()];
        const auto ground_action_index = unary_action.get_unrelaxed_action()->get_index();
        if (std::find(achievers.begin(), achievers.end(), ground_action_index) == achievers.end())
        {
            achievers.push_back(ground_action_index);
        }
    }

    auto first_achiever_action_indices_by_atom = std::vector<IndexList>(num_fluent_atoms);
    for (Index atom_index = 0; atom_index < num_fluent_atoms; ++atom_index)
    {
        auto& first_achievers = first_achiever_action_indices_by_atom[atom_index];
        for (const auto unary_action_index : action_achievers_by_proposition[positive_fluent_offsets[atom_index]])
        {
            const auto ground_action_index = get<Action>(structures)[unary_action_index].get_unrelaxed_action()->get_index();
            if (std::find(first_achievers.begin(), first_achievers.end(), ground_action_index) == first_achievers.end())
            {
                first_achievers.push_back(ground_action_index);
            }
        }
    }

    /**
     * Step 7-8: invert achiever/first-achiever/unique-achiever roles into per-action indices, sized
     * to the ground action repository (post-grounding, so every action `create_ground_actions()`
     * interned has a slot).
     */

    const auto& ground_action_repository = boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto num_ground_actions = ground_action_repository.size();

    auto landmarks_achieved_by_action = std::vector<IndexList>(num_ground_actions);
    auto landmarks_first_achieved_by_action = std::vector<IndexList>(num_ground_actions);
    auto landmarks_uniquely_achieved_by_action = std::vector<IndexList>(num_ground_actions);

    for (const auto landmark_atom_index : landmark_atom_indices)
    {
        const auto& achievers = achiever_action_indices_by_atom[landmark_atom_index];
        for (const auto ground_action_index : achievers)
        {
            landmarks_achieved_by_action[ground_action_index].push_back(landmark_atom_index);
        }
        for (const auto ground_action_index : first_achiever_action_indices_by_atom[landmark_atom_index])
        {
            landmarks_first_achieved_by_action[ground_action_index].push_back(landmark_atom_index);
        }
        if (achievers.size() == 1)
        {
            landmarks_uniquely_achieved_by_action[achievers.front()].push_back(landmark_atom_index);
        }
    }

    return std::make_shared<const FactLandmarkGraphImpl>(problem,
                                                          std::move(landmark_atom_mask),
                                                          std::move(landmark_atom_indices),
                                                          std::move(disjunctive_landmarks),
                                                          std::move(achiever_action_indices_by_atom),
                                                          std::move(first_achiever_action_indices_by_atom),
                                                          std::move(landmarks_achieved_by_action),
                                                          std::move(landmarks_first_achieved_by_action),
                                                          std::move(landmarks_uniquely_achieved_by_action),
                                                          std::move(predecessors_by_atom),
                                                          std::move(successors_by_atom));
}

}
