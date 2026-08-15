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

/// Tests for the LIW(1) action-level novelty precheck.
///
/// The precheck answers "could the successor of this action survive novelty pruning?" from the
/// transition's atom-level delta alone, without building the successor. Two things therefore have
/// to hold, and they are tested separately:
///
///   * it must agree with testing the real successor, on every transition, and
///   * it must not build one.
///
/// The first is checked exhaustively rather than only on hand-picked cases: a search is replayed
/// transition by transition and the two answers compared. Hand-picked cases still appear, for the
/// four ways a plausible implementation goes wrong -- the delete-list overlap, a flipped coordinate
/// with no novel atom of its own, the `BOT` coordinate flipping with no landmark in the adds, and
/// pairing a flipped coordinate against the predecessor's atoms instead of the successor's.

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <deque>
#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

using namespace mimir::formalism;
using namespace mimir::search;
using namespace mimir::search::landmarks;

namespace mimir::tests
{
namespace
{

struct Fixture
{
    Problem problem;
    LiftedGrounder grounder;
    GroundedAxiomEvaluator axiom_evaluator;
    StateRepository state_repository;
    GroundedApplicableActionGenerator action_generator;
    SearchContext context;
    FactLandmarkGraph landmarks;

    Fixture(const std::string& domain, const std::string& domain_file, const std::string& problem_file) :
        problem(ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain + "/" + domain_file),
                                    fs::path(std::string(DATA_DIR) + domain + "/" + problem_file))),
        grounder(problem),
        axiom_evaluator(grounder.create_grounded_axiom_evaluator()),
        state_repository(StateRepositoryImpl::create(axiom_evaluator)),
        action_generator(grounder.create_grounded_applicable_action_generator()),
        context(SearchContextImpl::create(problem, action_generator, state_repository)),
        landmarks(ApproximateFactLandmarkGenerator::create(grounder))
    {
    }

    size_t get_num_fluent_atoms() const
    {
        return boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {}).size();
    }

    Index get_atom_index(const std::string& name) const
    {
        const auto& repository = boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});
        for (const auto& atom : repository)
        {
            if (atom.get_predicate()->get_name() == name)
            {
                return atom.get_index();
            }
        }
        throw std::runtime_error("no ground fluent atom for predicate '" + name + "'");
    }

    GroundAction get_action(const State& state, const std::string& name) const
    {
        for (const auto action : action_generator->create_applicable_action_generator(state))
        {
            if (action->get_action()->get_name() == name)
            {
                return action;
            }
        }
        throw std::runtime_error("action '" + name + "' is not applicable here");
    }
};

struct RunResult
{
    SearchStatus status;
    std::vector<Index> plan_action_indices;
};

RunResult run_iw1(const Fixture& fixture, bool precheck)
{
    auto options = iw::Options {};
    options.max_arity = 1;
    options.iw_event_handler = iw::DefaultEventHandlerImpl::create(fixture.problem, true);
    options.landmark_novelty_graph = fixture.landmarks;
    options.iw1_precheck_add_effect_novelty = precheck;
    options.max_num_states = 500000;

    const auto result = iw::find_solution(fixture.context, options);

    auto plan_action_indices = std::vector<Index> {};
    if (result.plan.has_value())
    {
        for (const auto action : result.plan->get_actions())
        {
            plan_action_indices.push_back(action->get_index());
        }
    }
    return RunResult { result.status, std::move(plan_action_indices) };
}

/// @brief Walk the reachable states breadth-first and, at every state/action pair, compare the
/// precheck's verdict against testing the real successor with the same table.
///
/// The table is threaded through unchanged: both queries are read-only, so replaying a whole search
/// keeps the two in lockstep over a table that grows exactly as the real search's would.
void expect_precheck_agrees_with_successor_test(Fixture& fixture, size_t max_states)
{
    auto strategy = iw::LandmarkNoveltyPruningStrategyImpl(fixture.landmarks, 1, fixture.get_num_fluent_atoms());
    ASSERT_TRUE(strategy.supports_action_add_effect_precheck());
    ASSERT_TRUE(strategy.precheck_requires_delete_effects());

    const auto [initial_state, initial_g_value] = fixture.state_repository->get_or_create_initial_state();
    strategy.test_prune_initial_state(initial_state);

    auto queue = std::deque<State> { initial_state };
    auto seen = std::unordered_set<Index> { initial_state.get_index() };

    auto add_atom_indices = iw::AtomIndexList {};
    auto del_atom_indices = iw::AtomIndexList {};
    auto num_agreements = size_t(0);
    auto num_flipping_transitions = size_t(0);

    while (!queue.empty() && (seen.size() < max_states))
    {
        const auto state = queue.front();
        queue.pop_front();

        auto actions = std::vector<GroundAction> {};
        for (const auto action : fixture.action_generator->create_applicable_action_generator(state))
        {
            actions.push_back(action);
        }

        for (const auto action : actions)
        {
            fixture.state_repository->collect_action_change_effect_fluent_atom_indices(state, action, add_atom_indices, del_atom_indices);
            const auto precheck_verdict = strategy.test_transition_novelty_from_add_effects(state, add_atom_indices, del_atom_indices);

            const auto [succ_state, succ_g_value] = fixture.state_repository->get_or_create_successor_state(state, action, 0);

            /* The delta must describe the very successor the repository builds; the precheck's
               exactness rests on it, and the both-effects overlap is exactly where it used to fail. */
            auto reconstructed = std::vector<Index> {};
            for (const auto atom_index : state.get_atoms<FluentTag>())
            {
                if (std::find(del_atom_indices.begin(), del_atom_indices.end(), atom_index) == del_atom_indices.end())
                {
                    reconstructed.push_back(atom_index);
                }
            }
            reconstructed.insert(reconstructed.end(), add_atom_indices.begin(), add_atom_indices.end());
            std::sort(reconstructed.begin(), reconstructed.end());
            auto actual = std::vector<Index>(succ_state.get_atoms<FluentTag>().begin(), succ_state.get_atoms<FluentTag>().end());
            EXPECT_EQ(reconstructed, actual) << "delta does not reconstruct the successor of " << action->get_action()->get_name();

            const auto successor_verdict = const_cast<iw::LandmarkNoveltyTable&>(strategy.get_novelty_table()).test_novelty_read_only(state, succ_state);
            EXPECT_EQ(precheck_verdict, successor_verdict)
                << "precheck disagrees on " << action->get_action()->get_name() << " at state " << state.get_index();
            ++num_agreements;

            auto flipped = std::vector<uint32_t> {};
            auto kept = std::vector<uint32_t> {};
            strategy.get_novelty_table().collect_landmark_ranks(state, kept);
            {
                auto succ_ranks = std::vector<uint32_t> {};
                strategy.get_novelty_table().collect_landmark_ranks(succ_state, succ_ranks);
                for (const auto rank : succ_ranks)
                {
                    if (std::find(kept.begin(), kept.end(), rank) == kept.end())
                    {
                        flipped.push_back(rank);
                    }
                }
            }
            num_flipping_transitions += flipped.empty() ? 0 : 1;

            /* Only now commit the transition, so both queries above saw the same table state. */
            if (!strategy.test_prune_successor_state(state, succ_state, seen.find(succ_state.get_index()) == seen.end()))
            {
                if (seen.insert(succ_state.get_index()).second)
                {
                    queue.push_back(succ_state);
                }
            }
            else
            {
                seen.insert(succ_state.get_index());
            }
        }
    }

    EXPECT_GT(num_agreements, 0u) << "the walk never got to compare anything";
    EXPECT_GT(num_flipping_transitions, 0u) << "no coordinate ever flipped, so the expensive branch went untested";
}

const std::vector<std::pair<std::string, std::string>>& agreement_instances()
{
    static const auto instances = std::vector<std::pair<std::string, std::string>> { { "blocks_3", "test_problem.pddl" },
                                                                                     { "gripper", "test_problem.pddl" },
                                                                                     { "spanner", "test_problem.pddl" },
                                                                                     { "delivery", "test_problem.pddl" },
                                                                                     { "miconic", "test_problem.pddl" },
                                                                                     { "visitall", "test_problem.pddl" } };
    return instances;
}

}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckAgreesWithSuccessorTest)
{
    for (const auto& [domain, problem_file] : agreement_instances())
    {
        auto fixture = Fixture(domain, "domain.pddl", problem_file);
        ASSERT_GT(fixture.landmarks->get_landmark_atom_indices().size(), 0u) << domain;

        SCOPED_TRACE(domain);
        expect_precheck_agrees_with_successor_test(fixture, 2000);
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckAgreesOnConditionalEffects)
{
    /* Same property on the general branch, where the effects that fire have to be resolved against
       the state instead of read off the cache. */
    auto fixture = Fixture("landmark_cond_effect_dedup", "domain.pddl", "test_problem.pddl");
    expect_precheck_agrees_with_successor_test(fixture, 2000);
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckDoesNotChangeTheSearchResult)
{
    /* The precheck may only remove work, never outcomes: same verdict and same plan, action for
       action. Expansion counts may legitimately differ -- pruning an action before generating its
       successor is the point -- but the plan must not. */
    for (const auto& [domain, problem_file] : agreement_instances())
    {
        auto without_precheck = Fixture(domain, "domain.pddl", problem_file);
        auto with_precheck = Fixture(domain, "domain.pddl", problem_file);

        const auto baseline = run_iw1(without_precheck, false);
        const auto accelerated = run_iw1(with_precheck, true);

        EXPECT_EQ(accelerated.status, baseline.status) << domain;
        EXPECT_EQ(accelerated.plan_action_indices.size(), baseline.plan_action_indices.size()) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckBuildsNoSuccessorState)
{
    /* The requirement is not "does not call `get_or_create_successor_state`" but that no successor
       is constructed by any route -- the staged API is four more doors into the same room. The
       repository counts all of them, so a later refactor that reroutes through staging fails here
       instead of quietly costing the accelerator its entire purpose. */
    auto fixture = Fixture("blocks_3", "domain.pddl", "test_problem.pddl");

    auto strategy = iw::LandmarkNoveltyPruningStrategyImpl(fixture.landmarks, 1, fixture.get_num_fluent_atoms());
    const auto [initial_state, initial_g_value] = fixture.state_repository->get_or_create_initial_state();
    strategy.test_prune_initial_state(initial_state);

    auto actions = std::vector<GroundAction> {};
    for (const auto action : fixture.action_generator->create_applicable_action_generator(initial_state))
    {
        actions.push_back(action);
    }
    ASSERT_GT(actions.size(), 0u);

    const auto constructions_before = fixture.state_repository->get_num_successor_state_constructions();

    auto add_atom_indices = iw::AtomIndexList {};
    auto del_atom_indices = iw::AtomIndexList {};
    for (const auto action : actions)
    {
        fixture.state_repository->collect_action_change_effect_fluent_atom_indices(initial_state, action, add_atom_indices, del_atom_indices);
        strategy.test_transition_novelty_from_add_effects(initial_state, add_atom_indices, del_atom_indices);
        fixture.state_repository->get_unconditional_fluent_effect_atoms(action);
    }

    EXPECT_EQ(fixture.state_repository->get_num_successor_state_constructions(), constructions_before)
        << "the precheck built " << (fixture.state_repository->get_num_successor_state_constructions() - constructions_before) << " successor state(s)";
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckDeleteListMeansBecameFalse)
{
    /* `both` has `p` in the applied positive AND negative effect sets. Positives win, so `p`
       survives; a delete list classified by "true in s" alone would report it deleted and the
       reconstruction would drop an atom the successor keeps. */
    auto fixture = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");

    const auto [state, g_value] = fixture.state_repository->get_or_create_initial_state();
    const auto action = fixture.get_action(state, "both");
    const auto p_index = fixture.get_atom_index("p");
    const auto done_index = fixture.get_atom_index("done");

    auto add_atom_indices = iw::AtomIndexList {};
    auto del_atom_indices = iw::AtomIndexList {};
    fixture.state_repository->collect_action_change_effect_fluent_atom_indices(state, action, add_atom_indices, del_atom_indices);

    EXPECT_EQ(add_atom_indices, iw::AtomIndexList { done_index });
    EXPECT_TRUE(del_atom_indices.empty()) << "p is re-added by the same action, so nothing became false";

    const auto [succ_state, succ_g_value] = fixture.state_repository->get_or_create_successor_state(state, action, g_value);
    EXPECT_TRUE(succ_state.get_atoms<FluentTag>().get(p_index)) << "the successor really does keep p";

    auto reconstructed = std::vector<Index> {};
    for (const auto atom_index : state.get_atoms<FluentTag>())
    {
        if (std::find(del_atom_indices.begin(), del_atom_indices.end(), atom_index) == del_atom_indices.end())
        {
            reconstructed.push_back(atom_index);
        }
    }
    reconstructed.insert(reconstructed.end(), add_atom_indices.begin(), add_atom_indices.end());
    std::sort(reconstructed.begin(), reconstructed.end());

    const auto actual = std::vector<Index>(succ_state.get_atoms<FluentTag>().begin(), succ_state.get_atoms<FluentTag>().end());
    EXPECT_EQ(reconstructed, actual);
}

namespace
{
/// @brief Drives a `LandmarkNoveltyTable` over an explicitly chosen landmark set.
///
/// The generated landmark graph is deliberately not used here. These scenarios turn on exactly
/// which atoms are landmarks and on the precise contents of the table when the interesting action
/// is reached, and a generator that decided to call one more atom a landmark would silently turn a
/// discriminating case into a trivially-true one. `LandmarkNoveltyPruningStrategyImpl` forwards
/// both queries to this table verbatim, so testing it here tests the precheck.
class ScenarioDriver
{
public:
    ScenarioDriver(Fixture& fixture, const std::vector<std::string>& landmark_predicates) :
        m_fixture(&fixture),
        m_table(to_atom_indices(fixture, landmark_predicates), 1, fixture.get_num_fluent_atoms()),
        m_state(fixture.state_repository->get_or_create_initial_state().first)
    {
        m_table.test_novelty_and_update_table(m_state);
    }

    const State& get_state() const { return m_state; }

    /// @brief What the precheck answers for `action_name` at the current state, asserted to agree
    /// with what testing the real successor would answer.
    bool precheck(const std::string& action_name)
    {
        const auto action = m_fixture->get_action(m_state, action_name);
        m_fixture->state_repository->collect_action_change_effect_fluent_atom_indices(m_state, action, m_add_atom_indices, m_del_atom_indices);
        const auto verdict = m_table.test_novelty_read_only_from_delta(m_state, m_add_atom_indices, m_del_atom_indices);

        const auto [succ_state, succ_g_value] = m_fixture->state_repository->get_or_create_successor_state(m_state, action, 0);
        EXPECT_EQ(verdict, m_table.test_novelty_read_only(m_state, succ_state)) << "precheck and successor test disagree on " << action_name;
        return verdict;
    }

    /// @brief Apply `action_name` and mark the transition, as the search would.
    void advance(const std::string& action_name)
    {
        const auto action = m_fixture->get_action(m_state, action_name);
        const auto [succ_state, succ_g_value] = m_fixture->state_repository->get_or_create_successor_state(m_state, action, 0);
        m_table.test_novelty_and_update_table(m_state, succ_state);
        m_state = succ_state;
    }

    const iw::AtomIndexList& get_last_add_atom_indices() const { return m_add_atom_indices; }

private:
    static iw::AtomIndexList to_atom_indices(const Fixture& fixture, const std::vector<std::string>& predicates)
    {
        auto atom_indices = iw::AtomIndexList {};
        for (const auto& predicate : predicates)
        {
            atom_indices.push_back(fixture.get_atom_index(predicate));
        }
        return atom_indices;
    }

    Fixture* m_fixture;
    iw::LandmarkNoveltyTable m_table;
    State m_state;
    iw::AtomIndexList m_add_atom_indices;
    iw::AtomIndexList m_del_atom_indices;
};
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckKeepsNewlyFlippedCoordinate)
{
    /* `flip-lm1` switches the landmark on and adds nothing else. A newly true coordinate pairs with
       EVERY atom of the successor, so the transition is novel even though its only added atom is
       the landmark itself. An implementation that probes `kept x Add` alone finds `kept` empty here
       and prunes an admissible action. */
    auto fixture = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");
    auto driver = ScenarioDriver(fixture, { "lm1" });

    EXPECT_TRUE(driver.precheck("flip-lm1"));
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckProbesSuccessorAtomsNotPredecessorAtoms)
{
    /* Build a table in which `lm1` has already been paired with every atom of `{p, q}`, then apply
       an action that adds `lm1` and `r` together. The only unseen pair is `(lm1, {r})`, and `r` is
       not an atom of the predecessor -- so probing `flipped x atoms(s)` instead of
       `flipped x atoms(s')` misses it and prunes. */
    auto fixture = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");
    auto driver = ScenarioDriver(fixture, { "lm1" });

    driver.advance("flip-lm1");  // marks lm1 x {p, q, lm1, empty}
    driver.advance("drop-lm1");  // back to {p, q}

    EXPECT_FALSE(driver.precheck("flip-lm1")) << "guard: with r absent, lm1 x atoms(s) is exhausted";
    EXPECT_TRUE(driver.precheck("flip-lm1-and-add-r")) << "the pair (lm1, {r}) exists only in the successor's atoms";
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckDetectsBotFlippingOn)
{
    /* `Add & L = {}` does NOT imply that nothing flipped. `BOT` is a coordinate exactly when no real
       landmark is true, so an action deleting the last true landmark flips it on with no landmark
       among the adds at all. Here `(BOT, {r})` is unseen, because `r` only ever appeared while
       `lm1` was true. */
    auto fixture = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");
    auto driver = ScenarioDriver(fixture, { "lm1" });

    driver.advance("flip-lm1");  // {p, q, lm1}
    driver.advance("add-r");     // {p, q, lm1, r}: marks (lm1, {r}), never (BOT, {r})

    EXPECT_TRUE(driver.precheck("drop-lm1")) << "BOT flips on and (BOT, {r}) has never been marked";
    EXPECT_TRUE(driver.get_last_add_atom_indices().empty()) << "the case is only interesting when the action adds nothing";
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckPrunesWhenNothingIsNew)
{
    /* The complement: the precheck must actually prune, or the tests above would pass for a
       strategy that just answers true. Re-reaching a fully seen successor is not novel. */
    auto fixture = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");
    auto driver = ScenarioDriver(fixture, { "lm1" });

    driver.advance("flip-lm1");
    driver.advance("drop-lm1");

    EXPECT_FALSE(driver.precheck("flip-lm1")) << "every pair of this transition was marked the first time round";
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkPrecheckBranchesAgree)
{
    /* The cached-effect branch and the conditional-effect branch must differ only in cost. The two
       domains encode the same transition system, one with unconditional effects and one where every
       effect is wrapped in a condition that always holds, so the same walk must produce the same
       verdicts on both. */
    auto unconditional = Fixture("liw_precheck", "domain.pddl", "test_problem.pddl");
    auto conditional = Fixture("liw_precheck", "conditional_domain.pddl", "conditional_problem.pddl");

    const auto collect_verdicts = [](Fixture& fixture)
    {
        auto strategy = iw::LandmarkNoveltyPruningStrategyImpl(fixture.landmarks, 1, fixture.get_num_fluent_atoms());
        const auto [initial_state, initial_g_value] = fixture.state_repository->get_or_create_initial_state();
        strategy.test_prune_initial_state(initial_state);

        auto verdicts = std::vector<std::pair<std::string, bool>> {};
        auto add_atom_indices = iw::AtomIndexList {};
        auto del_atom_indices = iw::AtomIndexList {};
        auto queue = std::deque<State> { initial_state };
        auto seen = std::unordered_set<Index> { initial_state.get_index() };

        while (!queue.empty())
        {
            const auto state = queue.front();
            queue.pop_front();

            auto actions = std::vector<GroundAction> {};
            for (const auto action : fixture.action_generator->create_applicable_action_generator(state))
            {
                actions.push_back(action);
            }
            /* Action names, not indices: the two domains ground independently. */
            std::sort(actions.begin(),
                      actions.end(),
                      [](GroundAction a, GroundAction b) { return a->get_action()->get_name() < b->get_action()->get_name(); });

            for (const auto action : actions)
            {
                fixture.state_repository->collect_action_change_effect_fluent_atom_indices(state, action, add_atom_indices, del_atom_indices);
                verdicts.emplace_back(action->get_action()->get_name(),
                                      strategy.test_transition_novelty_from_add_effects(state, add_atom_indices, del_atom_indices));

                const auto [succ_state, succ_g_value] = fixture.state_repository->get_or_create_successor_state(state, action, 0);
                const auto is_new = (seen.find(succ_state.get_index()) == seen.end());
                if (!strategy.test_prune_successor_state(state, succ_state, is_new) && seen.insert(succ_state.get_index()).second)
                {
                    queue.push_back(succ_state);
                }
                seen.insert(succ_state.get_index());
            }
        }
        return verdicts;
    };

    const auto unconditional_verdicts = collect_verdicts(unconditional);
    const auto conditional_verdicts = collect_verdicts(conditional);

    ASSERT_FALSE(unconditional_verdicts.empty());
    EXPECT_EQ(unconditional_verdicts, conditional_verdicts);
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltySupportsTransitionWitnessQuery)
{
    /* The witness query reports the atoms carrying an unseen pair. On a fresh table every atom of
       the successor qualifies, since no pair has been marked yet. */
    auto fixture = Fixture("blocks_3", "domain.pddl", "test_problem.pddl");

    auto strategy = iw::LandmarkNoveltyPruningStrategyImpl(fixture.landmarks, 1, fixture.get_num_fluent_atoms());
    EXPECT_TRUE(strategy.supports_transition_novel_witness_query());

    const auto [state, g_value] = fixture.state_repository->get_or_create_initial_state();
    auto action = GroundAction(nullptr);
    for (const auto candidate : fixture.action_generator->create_applicable_action_generator(state))
    {
        action = candidate;
        break;
    }
    ASSERT_NE(action, nullptr);
    const auto [succ_state, succ_g_value] = fixture.state_repository->get_or_create_successor_state(state, action, g_value);

    auto witnesses = iw::AtomIndexList {};
    strategy.compute_transition_novel_fluent_atom_indices_read_only(state, succ_state, witnesses);
    const auto all_succ_atoms = iw::AtomIndexList(succ_state.get_atoms<FluentTag>().begin(), succ_state.get_atoms<FluentTag>().end());
    EXPECT_EQ(witnesses, all_succ_atoms) << "nothing is marked yet, so every successor atom carries an unseen pair";

    /* After the transition is marked, the pairs it produced are seen, so the atoms that only ever
       appeared in those pairs stop being witnesses. */
    strategy.test_prune_initial_state(state);
    strategy.test_prune_successor_state(state, succ_state, true);
    strategy.compute_transition_novel_fluent_atom_indices_read_only(state, succ_state, witnesses);
    EXPECT_TRUE(witnesses.empty()) << "the transition was just marked, so it has no unseen pair left";
}

}
