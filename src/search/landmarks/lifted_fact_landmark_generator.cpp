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

#include "mimir/search/landmarks/lifted_fact_landmark_generator.hpp"

#include "mimir/formalism/action.hpp"
#include "mimir/formalism/atom.hpp"
#include "mimir/formalism/conjunctive_condition.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/literal.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/parameter.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/formalism/term.hpp"
#include "mimir/formalism/type.hpp"
#include "mimir/formalism/variable.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>

using namespace mimir::formalism;

namespace mimir::search::landmarks
{
namespace
{

/// @brief Marks a free position of a partially ground atom in an `Identity`, and a term that is not
/// a variable in `term_slot`. Wherever an `Object` is stored, `nullptr` plays the same role.
constexpr Index FREE_POSITION = std::numeric_limits<Index>::max();

/// @brief mimir compiles PDDL equality into an ordinary static predicate named "=", but whether its
/// atoms are *materialised* in `get_static_initial_atoms()` depends on the domain: `data/childsnack`
/// has seven of them (one per object), `data/ipc/childsnack-ipc` has none. Looking `=` up in the
/// static atom table would therefore drop achievers on a domain whose equality atoms were never
/// materialised, and dropping an achiever that can fire is the one thing here that is unsound. Its
/// semantics are object identity, so evaluate it instead of looking it up.
bool is_equality_predicate(Predicate<StaticTag> predicate) { return predicate->get_name() == "="; }

/// @brief The identity of a partially ground atom (and, reused, of a ground static atom):
/// `[predicate index, position 0, ..., position n-1]` with `FREE_POSITION` for a free position.
using Identity = std::vector<Index>;

struct IdentityHash
{
    size_t operator()(const Identity& key) const
    {
        auto hash = size_t(1469598103934665603ull);
        for (const auto value : key)
        {
            hash = (hash ^ size_t(value)) * size_t(1099511628211ull);
        }
        return hash;
    }
};

Identity make_identity(Index predicate_index, const ObjectList& binding)
{
    auto key = Identity {};
    key.reserve(binding.size() + 1);
    key.push_back(predicate_index);
    for (const auto object : binding)
    {
        key.push_back(object ? object->get_index() : FREE_POSITION);
    }
    return key;
}

bool is_fully_bound(const ObjectList& binding)
{
    return std::all_of(binding.begin(), binding.end(), [](Object object) { return object != nullptr; });
}

bool by_object_index(Object lhs, Object rhs) { return lhs->get_index() < rhs->get_index(); }

void sort_unique(IndexList& indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

/// @brief One `(A, E)` pair with its literals pre-split, so the worklist loop never rewalks a
/// schema. A slot is `Variable::get_parameter_index()`: the grounder's binding vector holds the
/// action parameters first and appends the conditional effect's own parameters, and the parameters
/// report exactly that index, so the layout is read off the model rather than assumed.
struct SchemaEffect
{
    Action action;
    ConditionalEffect effect;
    size_t num_slots;
    ParameterList parameter_by_slot;
    LiteralList<FluentTag> positive_fluent_preconditions;
    LiteralList<StaticTag> static_literals;
    LiteralList<FluentTag> positive_fluent_effects;
};

/// @brief An achiever `(A, E, sigma)` of the partially ground atom being expanded, together with
/// the per-slot candidate domains the static filter narrowed.
struct Achiever
{
    const SchemaEffect* schema;
    ObjectList sigma;                    ///< `nullptr` marks an unbound slot.
    std::vector<ObjectList> candidates;  ///< Per slot, ascending by object index; unused when bound.
};

/// @brief Everything about the problem the extraction rule reads, indexed once.
class ProblemIndex
{
public:
    explicit ProblemIndex(const Problem& problem)
    {
        for (const auto atom : problem->get_static_initial_atoms())
        {
            m_static_atoms_by_predicate[atom->get_predicate()->get_index()].push_back(atom);
            m_static_atom_identities.insert(make_identity(atom->get_predicate()->get_index(), atom->get_objects()));
        }
        for (const auto atom : problem->get_fluent_initial_atoms())
        {
            m_fluent_initial_by_predicate[atom->get_predicate()->get_index()].push_back(atom);
        }
        for (const auto object : problem->get_problem_and_domain_objects())
        {
            m_all_objects.push_back(object);
        }
        std::sort(m_all_objects.begin(), m_all_objects.end(), by_object_index);
    }

    const GroundAtomList<StaticTag>& get_static_atoms(Index predicate_index) const
    {
        static const auto empty = GroundAtomList<StaticTag> {};
        const auto it = m_static_atoms_by_predicate.find(predicate_index);
        return (it == m_static_atoms_by_predicate.end()) ? empty : it->second;
    }

    const GroundAtomList<FluentTag>& get_fluent_initial_atoms(Index predicate_index) const
    {
        static const auto empty = GroundAtomList<FluentTag> {};
        const auto it = m_fluent_initial_by_predicate.find(predicate_index);
        return (it == m_fluent_initial_by_predicate.end()) ? empty : it->second;
    }

    bool has_static_atom(Index predicate_index, const ObjectList& objects) const
    {
        return m_static_atom_identities.count(make_identity(predicate_index, objects)) > 0;
    }

    /// @brief The objects a parameter may take, by its declared types; every object when the
    /// parameter is unknown (which the model should never produce, but a wrong guess here must cost
    /// precision, not soundness).
    ///
    /// mimir's parser also compiles the type hierarchy into unary static predicates that appear in
    /// every action's static condition (`(sandwich ?s)`, `(object ?s)`), so the static filter would
    /// narrow a free variable to the same set. The declared types are used instead because they are
    /// part of the model rather than of the translation, and because they bound the candidate set
    /// *before* the filter runs -- which is what keeps member instantiation from enumerating
    /// |objects|^arity tuples on a large instance only to throw all but a few of them away.
    const ObjectList& get_candidate_objects(Parameter parameter) const
    {
        if (!parameter)
        {
            return m_all_objects;
        }
        const auto it = m_objects_by_parameter.find(parameter);
        if (it != m_objects_by_parameter.end())
        {
            return it->second;
        }
        auto objects = ObjectList {};
        for (const auto object : m_all_objects)
        {
            if (is_subtypeeq(object->get_bases(), parameter->get_bases()))
            {
                objects.push_back(object);
            }
        }
        return m_objects_by_parameter.emplace(parameter, std::move(objects)).first->second;
    }

    bool is_type_compatible(Object object, Parameter parameter) const
    {
        return !parameter || is_subtypeeq(object->get_bases(), parameter->get_bases());
    }

private:
    ObjectList m_all_objects;
    std::unordered_map<Index, GroundAtomList<StaticTag>> m_static_atoms_by_predicate;
    std::unordered_map<Index, GroundAtomList<FluentTag>> m_fluent_initial_by_predicate;
    std::unordered_set<Identity, IdentityHash> m_static_atom_identities;
    mutable std::unordered_map<Parameter, ObjectList> m_objects_by_parameter;
};

/// @brief Collect the `(A, E)` pairs with their literals split by tag and polarity.
std::vector<SchemaEffect> collect_schema_effects(const Problem& problem)
{
    auto result = std::vector<SchemaEffect> {};

    for (const auto action : problem->get_domain()->get_actions())
    {
        for (const auto effect : action->get_conditional_effects())
        {
            auto schema = SchemaEffect { action, effect, 0, {}, {}, {}, {} };

            const auto register_parameters = [&](const ParameterList& parameters)
            {
                for (const auto parameter : parameters)
                {
                    const auto slot = parameter->get_variable()->get_parameter_index();
                    if (slot >= schema.parameter_by_slot.size())
                    {
                        schema.parameter_by_slot.resize(slot + 1, nullptr);
                    }
                    schema.parameter_by_slot[slot] = parameter;
                }
            };
            register_parameters(action->get_conjunctive_condition()->get_parameters());
            register_parameters(effect->get_conjunctive_condition()->get_parameters());
            register_parameters(effect->get_conjunctive_effect()->get_parameters());
            schema.num_slots = schema.parameter_by_slot.size();

            const auto collect_condition = [&](ConjunctiveCondition condition)
            {
                for (const auto literal : condition->get_literals<FluentTag>())
                {
                    if (literal->get_polarity())
                    {
                        schema.positive_fluent_preconditions.push_back(literal);
                    }
                }
                for (const auto literal : condition->get_literals<StaticTag>())
                {
                    schema.static_literals.push_back(literal);
                }
            };
            collect_condition(action->get_conjunctive_condition());
            collect_condition(effect->get_conjunctive_condition());

            for (const auto literal : effect->get_conjunctive_effect()->get_literals())
            {
                if (literal->get_polarity())
                {
                    schema.positive_fluent_effects.push_back(literal);
                }
            }

            result.push_back(std::move(schema));
        }
    }

    return result;
}

/// @brief The object a term denotes under `sigma`, or `nullptr` if it is still free.
Object resolve_term(Term term, const ObjectList& sigma)
{
    const auto& variant = term->get_variant();
    if (std::holds_alternative<Object>(variant))
    {
        return std::get<Object>(variant);
    }
    const auto slot = std::get<Variable>(variant)->get_parameter_index();
    return (slot < sigma.size()) ? sigma[slot] : nullptr;
}

/// @brief The binding slot of a term, or `FREE_POSITION` when the term is a constant.
Index term_slot(Term term)
{
    const auto& variant = term->get_variant();
    return std::holds_alternative<Variable>(variant) ? std::get<Variable>(variant)->get_parameter_index() : FREE_POSITION;
}

/// @brief §2.3 for one positive static literal: keep only the static atoms consistent with `sigma`
/// and the current candidate domains, and narrow the domains of the variables the literal mentions.
/// Returns false when the literal has no consistent instance at all -- the achiever is then dead.
bool narrow_by_positive_static_literal(Literal<StaticTag> literal, const ProblemIndex& index, const ObjectList& sigma, std::vector<ObjectList>& candidates, bool& changed)
{
    const auto& terms = literal->get_atom()->get_terms();
    const auto predicate_index = literal->get_atom()->get_predicate()->get_index();

    auto narrowed = std::unordered_map<Index, ObjectList> {};
    auto any_match = false;

    for (const auto atom : index.get_static_atoms(predicate_index))
    {
        const auto& objects = atom->get_objects();
        if (objects.size() != terms.size())
        {
            continue;
        }

        /* A variable may occur at several positions of one literal; the atom has to agree with
           itself there, which a per-position test alone would not catch. */
        auto local = std::unordered_map<Index, Object> {};
        auto consistent = true;
        for (size_t i = 0; consistent && i < terms.size(); ++i)
        {
            const auto bound = resolve_term(terms[i], sigma);
            if (bound)
            {
                consistent = (bound == objects[i]);
                continue;
            }
            const auto slot = term_slot(terms[i]);
            if (slot >= candidates.size())
            {
                continue;  // not a parameter of this schema: cannot constrain anything
            }
            const auto& domain = candidates[slot];
            if (!std::binary_search(domain.begin(), domain.end(), objects[i], by_object_index))
            {
                consistent = false;
                break;
            }
            const auto [it, inserted] = local.emplace(slot, objects[i]);
            consistent = inserted || (it->second == objects[i]);
        }
        if (!consistent)
        {
            continue;
        }

        any_match = true;
        for (const auto& [slot, object] : local)
        {
            narrowed[slot].push_back(object);
        }
    }

    if (!any_match)
    {
        return false;
    }

    for (auto& [slot, objects] : narrowed)
    {
        std::sort(objects.begin(), objects.end(), by_object_index);
        objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
        if (objects.empty())
        {
            return false;
        }
        changed |= (objects.size() < candidates[slot].size());
        candidates[slot] = std::move(objects);
    }

    return true;
}

/// @brief §2.3 for the equality predicate, evaluated rather than looked up (see `is_equality_predicate`).
bool narrow_by_equality_literal(Literal<StaticTag> literal, const ObjectList& sigma, std::vector<ObjectList>& candidates, bool& changed)
{
    const auto& terms = literal->get_atom()->get_terms();
    if (terms.size() != 2)
    {
        return true;  // not the binary `=` whose semantics we know
    }

    const auto lhs = resolve_term(terms[0], sigma);
    const auto rhs = resolve_term(terms[1], sigma);

    if (!literal->get_polarity())
    {
        return !(lhs && rhs && lhs == rhs);
    }

    if (lhs && rhs)
    {
        return lhs == rhs;
    }

    // Exactly one side bound: the free side can only be that object.
    const auto bound = lhs ? lhs : rhs;
    const auto free_slot = lhs ? term_slot(terms[1]) : term_slot(terms[0]);
    if (bound && free_slot != FREE_POSITION && free_slot < candidates.size())
    {
        auto& domain = candidates[free_slot];
        if (std::find(domain.begin(), domain.end(), bound) == domain.end())
        {
            return false;
        }
        changed |= (domain.size() > 1);
        domain = ObjectList { bound };
    }
    return true;
}

/// @brief §2.3: iterate the static filter to a fixpoint. Returns false to drop the achiever.
bool apply_static_filter(Achiever& achiever, const ProblemIndex& index)
{
    auto changed = true;
    while (changed)
    {
        changed = false;

        for (const auto literal : achiever.schema->static_literals)
        {
            const auto predicate = literal->get_atom()->get_predicate();

            if (is_equality_predicate(predicate))
            {
                if (!narrow_by_equality_literal(literal, achiever.sigma, achiever.candidates, changed))
                {
                    return false;
                }
            }
            else if (literal->get_polarity())
            {
                if (!narrow_by_positive_static_literal(literal, index, achiever.sigma, achiever.candidates, changed))
                {
                    return false;
                }
            }
            else
            {
                /* A negative static literal proves something only once it is fully bound: a pattern
                   with a free position says "some instance is absent", which constrains nothing. */
                auto objects = ObjectList {};
                auto bound = true;
                for (const auto term : literal->get_atom()->get_terms())
                {
                    const auto object = resolve_term(term, achiever.sigma);
                    bound &= (object != nullptr);
                    objects.push_back(object);
                }
                if (bound && index.has_static_atom(predicate->get_index(), objects))
                {
                    return false;
                }
            }
        }

        /* Binding a variable that every statically consistent instance binds the same way is the
           point of the pass: it turns the achiever's other preconditions from partial into ground
           ones, which is what fixes the place through childsnack's `waiting(?c, ?p)`. */
        for (size_t slot = 0; slot < achiever.candidates.size(); ++slot)
        {
            if (!achiever.sigma[slot] && achiever.candidates[slot].size() == 1)
            {
                achiever.sigma[slot] = achiever.candidates[slot].front();
                changed = true;
            }
        }
    }

    return true;
}

/// @brief §2.5's per-member test: is `sigma` statically consistent for `schema`?
bool is_statically_consistent(const SchemaEffect& schema, const ObjectList& sigma, const ProblemIndex& index)
{
    for (const auto literal : schema.static_literals)
    {
        const auto predicate = literal->get_atom()->get_predicate();
        const auto& terms = literal->get_atom()->get_terms();

        auto objects = ObjectList {};
        auto bound = true;
        for (const auto term : terms)
        {
            const auto object = resolve_term(term, sigma);
            bound &= (object != nullptr);
            objects.push_back(object);
        }

        if (is_equality_predicate(predicate))
        {
            if (terms.size() == 2 && bound && ((objects[0] == objects[1]) != literal->get_polarity()))
            {
                return false;
            }
            continue;
        }

        if (!literal->get_polarity())
        {
            if (bound && index.has_static_atom(predicate->get_index(), objects))
            {
                return false;
            }
            continue;
        }

        if (bound)
        {
            if (!index.has_static_atom(predicate->get_index(), objects))
            {
                return false;
            }
            continue;
        }

        /* Still partial: an arc-consistent over-approximation -- some static atom has to agree on
           the positions that *are* bound. Over-approximating here only keeps members that no
           instance can produce, which costs precision and never soundness. */
        auto matched = false;
        for (const auto atom : index.get_static_atoms(predicate->get_index()))
        {
            const auto& atom_objects = atom->get_objects();
            if (atom_objects.size() != objects.size())
            {
                continue;
            }
            auto consistent = true;
            for (size_t i = 0; consistent && i < objects.size(); ++i)
            {
                consistent = !objects[i] || (objects[i] == atom_objects[i]);
            }
            if (consistent)
            {
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            return false;
        }
    }

    return true;
}

}

FactLandmarkGraph LiftedFactLandmarkGenerator::create(const Problem& problem, const LiftedFactLandmarkGeneratorOptions& options)
{
    const auto index = ProblemIndex(problem);
    const auto schemas = collect_schema_effects(problem);

    /**
     * State: one record per kept partially ground atom, a map from its identity to its position,
     * and a worklist of positions still to expand.
     */

    auto records = std::vector<LiftedLandmark> {};
    auto position_by_identity = std::unordered_map<Identity, size_t, IdentityHash> {};
    auto worklist = std::deque<size_t> {};

    auto landmark_atom_indices = IndexList {};
    auto predecessors_by_atom = std::vector<IndexList> {};
    auto successors_by_atom = std::vector<IndexList> {};

    const auto record_ordering_edge = [&](Index predecessor_atom, Index successor_atom)
    {
        const auto needed = size_t(std::max(predecessor_atom, successor_atom)) + 1;
        if (predecessors_by_atom.size() < needed)
        {
            predecessors_by_atom.resize(needed);
            successors_by_atom.resize(needed);
        }
        predecessors_by_atom[successor_atom].push_back(predecessor_atom);
        successors_by_atom[predecessor_atom].push_back(successor_atom);
    };

    /// Insert `predicate(binding)`, or merge into the record already holding that identity, and
    /// return its position; `nullopt` when a more specific landmark already subsumes it.
    const auto insert = [&](Predicate<FluentTag> predicate, const ObjectList& binding, IndexList members, std::optional<size_t> parent_position)
        -> std::optional<size_t>
    {
        const auto bound = is_fully_bound(binding);
        if (bound)
        {
            // A fully bound landmark's member list is exactly its own atom, by definition.
            members = IndexList { problem->get_or_create_ground_atom<FluentTag>(predicate, binding)->get_index() };
        }
        sort_unique(members);

        const auto identity = make_identity(predicate->get_index(), binding);
        const auto existing = position_by_identity.find(identity);
        if (existing != position_by_identity.end())
        {
            /* Each derivation of an identity is a sound member set on its own, so their union is
               sound too -- weaker, but both consumers only ever read the union. */
            auto& record = records[existing->second];
            record.member_atom_indices.insert(record.member_atom_indices.end(), members.begin(), members.end());
            sort_unique(record.member_atom_indices);
            if (parent_position.has_value()
                && std::find(record.parent_positions.begin(), record.parent_positions.end(), Index(*parent_position)) == record.parent_positions.end())
            {
                record.parent_positions.push_back(Index(*parent_position));
            }
            return existing->second;
        }

        /* §2.4 subsumption: a more specific landmark of the same predicate implies this one, and --
           its achiever set being a subset with more bound preconditions -- everything derivable
           here is a generalisation of something derivable from it. Only a partial landmark can be
           subsumed by a different identity, so a fact landmark never takes this branch. */
        if (!bound)
        {
            for (const auto& other : records)
            {
                if (other.predicate != predicate || other.binding.size() != binding.size())
                {
                    continue;
                }
                auto subsumes = true;
                for (size_t i = 0; subsumes && i < binding.size(); ++i)
                {
                    subsumes = !binding[i] || (other.binding[i] == binding[i]);
                }
                if (subsumes)
                {
                    return std::nullopt;
                }
            }
        }

        auto record = LiftedLandmark {};
        record.predicate = predicate;
        record.binding = binding;
        record.member_atom_indices = std::move(members);
        if (bound)
        {
            record.fact_atom_index = record.member_atom_indices.front();
            landmark_atom_indices.push_back(*record.fact_atom_index);
        }
        if (parent_position.has_value())
        {
            record.parent_positions.push_back(Index(*parent_position));
        }

        const auto position = records.size();
        records.push_back(std::move(record));
        position_by_identity.emplace(identity, position);
        worklist.push_back(position);
        return position;
    };

    /// `insert` plus §2.5's singleton promotion and the fact-fact ordering edge.
    const auto insert_derived = [&](Predicate<FluentTag> predicate, const ObjectList& binding, IndexList members, size_t parent_position)
    {
        auto effective_binding = binding;

        if (!is_fully_bound(binding))
        {
            sort_unique(members);
            if (members.empty())
            {
                /* No statically consistent achiever instance in this instance. If the parent really
                   is a landmark the task is unsolvable, which is not this code's business. */
                return;
            }
            if (members.size() == 1 && options.promote_singleton_disjunctions)
            {
                /* Every plan makes *some* member true and there is exactly one, so that member is
                   mandatory. The partial form is dropped rather than kept alongside: expanding it
                   too would rederive, more weakly, everything the promoted fact derives. */
                effective_binding = problem->get_repositories().get_ground_atom<FluentTag>(members.front())->get_objects();
            }
        }

        const auto position = insert(predicate, effective_binding, std::move(members), parent_position);
        if (!position.has_value())
        {
            return;
        }

        if (options.compute_greedy_necessary_orderings && records[*position].fact_atom_index.has_value()
            && records[parent_position].fact_atom_index.has_value())
        {
            record_ordering_edge(*records[*position].fact_atom_index, *records[parent_position].fact_atom_index);
        }
    };

    /**
     * Seed: the positive fluent goal atoms, in goal order.
     */

    if (options.include_positive_goal_facts)
    {
        for (const auto goal_atom_index : problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>())
        {
            const auto atom = problem->get_repositories().get_ground_atom<FluentTag>(goal_atom_index);
            insert(atom->get_predicate(), atom->get_objects(), IndexList {}, std::nullopt);
        }
    }

    /**
     * Expansion.
     */

    auto achievers = std::vector<Achiever> {};
    auto occurrences_by_achiever = std::vector<std::unordered_map<Index, LiteralList<FluentTag>>> {};

    while (!worklist.empty())
    {
        const auto position = worklist.front();
        worklist.pop_front();

        const auto predicate = records[position].predicate;
        const auto binding = records[position].binding;  // by value: `records` reallocates below

        /**
         * §2.1: an initially-true instance means the first state holding an instance of the pattern
         * need not have been produced by any action, so the induction step of Proposition 1 has no
         * base and nothing may be derived through it. The landmark itself stays (trivially
         * satisfied at I, parity with the grounded generator, whose back-chain stops here too).
         */

        auto initially_true = false;
        for (const auto atom : index.get_fluent_initial_atoms(predicate->get_index()))
        {
            const auto& objects = atom->get_objects();
            if (objects.size() != binding.size())
            {
                continue;
            }
            auto matches = true;
            for (size_t i = 0; matches && i < binding.size(); ++i)
            {
                matches = !binding[i] || (binding[i] == objects[i]);
            }
            if (matches)
            {
                initially_true = true;
                break;
            }
        }
        if (initially_true)
        {
            records[position].initially_true = true;
            continue;
        }

        /**
         * §2.2 + §2.3: the achievers of the pattern, statically filtered.
         */

        achievers.clear();
        for (const auto& schema : schemas)
        {
            for (const auto effect_literal : schema.positive_fluent_effects)
            {
                if (effect_literal->get_atom()->get_predicate() != predicate)
                {
                    continue;
                }

                auto achiever = Achiever { &schema, ObjectList(schema.num_slots, nullptr), {} };

                const auto& terms = effect_literal->get_atom()->get_terms();
                auto unifies = (terms.size() == binding.size());
                for (size_t i = 0; unifies && i < binding.size(); ++i)
                {
                    if (!binding[i])
                    {
                        continue;  // a free position of the pattern constrains nothing
                    }
                    const auto& variant = terms[i]->get_variant();
                    if (std::holds_alternative<Object>(variant))
                    {
                        unifies = (std::get<Object>(variant) == binding[i]);
                        continue;
                    }
                    const auto slot = std::get<Variable>(variant)->get_parameter_index();
                    unifies = (!achiever.sigma[slot] || achiever.sigma[slot] == binding[i])
                              && index.is_type_compatible(binding[i], schema.parameter_by_slot[slot]);
                    if (unifies)
                    {
                        achiever.sigma[slot] = binding[i];
                    }
                }
                if (!unifies)
                {
                    continue;
                }

                achiever.candidates.resize(schema.num_slots);
                for (size_t slot = 0; slot < schema.num_slots; ++slot)
                {
                    if (!achiever.sigma[slot])
                    {
                        achiever.candidates[slot] = index.get_candidate_objects(schema.parameter_by_slot[slot]);
                    }
                }

                if (options.use_static_filter && !apply_static_filter(achiever, index))
                {
                    continue;
                }

                achievers.push_back(std::move(achiever));
            }
        }

        if (achievers.empty())
        {
            continue;  // unachievable in this domain: keep the landmark, derive nothing
        }

        /**
         * §2.4: a fluent predicate that *every* achiever's precondition mentions, with position-wise
         * agreement over one chosen occurrence per achiever.
         */

        occurrences_by_achiever.assign(achievers.size(), {});
        for (size_t j = 0; j < achievers.size(); ++j)
        {
            for (const auto literal : achievers[j].schema->positive_fluent_preconditions)
            {
                occurrences_by_achiever[j][literal->get_atom()->get_predicate()->get_index()].push_back(literal);
            }
        }

        for (const auto& [precondition_predicate_index, first_occurrences] : occurrences_by_achiever.front())
        {
            auto occurrences = std::vector<const LiteralList<FluentTag>*> { &first_occurrences };
            auto in_every_achiever = true;
            for (size_t j = 1; j < achievers.size(); ++j)
            {
                const auto it = occurrences_by_achiever[j].find(precondition_predicate_index);
                if (it == occurrences_by_achiever[j].end())
                {
                    /* An achiever that does not need the predicate at all can discharge the parent
                       without touching any of its atoms, so nothing about it is a landmark. */
                    in_every_achiever = false;
                    break;
                }
                occurrences.push_back(&it->second);
            }
            if (!in_every_achiever)
            {
                continue;
            }

            /* Every choice vector is a sound landmark of its own and they genuinely differ, but
               their number is the product of the per-achiever occurrence counts. */
            auto num_combinations = size_t(1);
            for (const auto* occurrence : occurrences)
            {
                num_combinations = (num_combinations > options.max_occurrence_combinations / std::max<size_t>(occurrence->size(), 1)) ?
                                       std::numeric_limits<size_t>::max() :
                                       num_combinations * occurrence->size();
            }
            const auto enumerate_all = (options.max_occurrence_combinations > 1) && (num_combinations <= options.max_occurrence_combinations);
            const auto num_choice_vectors = enumerate_all ? num_combinations : size_t(1);

            for (size_t combination = 0; combination < num_choice_vectors; ++combination)
            {
                auto choice = std::vector<size_t>(occurrences.size(), 0);
                auto remainder = combination;
                for (size_t j = occurrences.size(); j-- > 0;)
                {
                    choice[j] = remainder % occurrences[j]->size();
                    remainder /= occurrences[j]->size();
                }

                const auto precondition_predicate = (*occurrences.front())[choice.front()]->get_atom()->get_predicate();
                const auto arity = precondition_predicate->get_arity();

                /* Position-wise agreement across the chosen occurrences -- never across *different*
                   occurrences of one achiever, which is unsound and is why the choice vector is a
                   vector at all. */
                auto derived_binding = ObjectList(arity, nullptr);
                for (size_t p = 0; p < arity; ++p)
                {
                    auto agreed = Object(nullptr);
                    auto agrees = true;
                    for (size_t j = 0; agrees && j < occurrences.size(); ++j)
                    {
                        const auto object = resolve_term((*occurrences[j])[choice[j]]->get_atom()->get_terms()[p], achievers[j].sigma);
                        agrees = object && (!agreed || agreed == object);
                        agreed = object;
                    }
                    derived_binding[p] = agrees ? agreed : nullptr;
                }

                /**
                 * §2.5: the ground vocabulary. For every achiever, complete its chosen literal over
                 * the candidate domains the static filter left and keep the statically consistent
                 * instantiations. The first achiever of the parent in any plan is an applicable
                 * ground instance of one of these -- applicable implies statically consistent and
                 * type-correct -- so its own instance of the literal is in this set.
                 */

                auto members = IndexList {};
                if (!is_fully_bound(derived_binding))
                {
                    for (size_t j = 0; j < occurrences.size(); ++j)
                    {
                        const auto& achiever = achievers[j];
                        const auto& terms = (*occurrences[j])[choice[j]]->get_atom()->get_terms();

                        auto free_slots = std::vector<Index> {};
                        for (const auto term : terms)
                        {
                            const auto slot = term_slot(term);
                            if (slot != FREE_POSITION && slot < achiever.sigma.size() && !achiever.sigma[slot]
                                && std::find(free_slots.begin(), free_slots.end(), slot) == free_slots.end())
                            {
                                free_slots.push_back(slot);
                            }
                        }
                        if (std::any_of(free_slots.begin(), free_slots.end(), [&](Index slot) { return achiever.candidates[slot].empty(); }))
                        {
                            continue;
                        }

                        auto completion = achiever.sigma;
                        auto odometer = std::vector<size_t>(free_slots.size(), 0);
                        auto exhausted = false;
                        while (!exhausted)
                        {
                            for (size_t k = 0; k < free_slots.size(); ++k)
                            {
                                completion[free_slots[k]] = achiever.candidates[free_slots[k]][odometer[k]];
                            }

                            if (is_statically_consistent(*achiever.schema, completion, index))
                            {
                                auto objects = ObjectList {};
                                objects.reserve(terms.size());
                                for (const auto term : terms)
                                {
                                    objects.push_back(resolve_term(term, completion));
                                }
                                members.push_back(problem->get_or_create_ground_atom<FluentTag>(precondition_predicate, objects)->get_index());
                            }

                            exhausted = true;
                            for (size_t k = free_slots.size(); k-- > 0;)
                            {
                                if (++odometer[k] < achiever.candidates[free_slots[k]].size())
                                {
                                    exhausted = false;
                                    break;
                                }
                                odometer[k] = 0;
                            }
                        }
                    }
                }

                insert_derived(precondition_predicate, derived_binding, std::move(members), position);
            }
        }
    }

    /**
     * Assemble the graph. A partial landmark contributes its member set; a set over the cap is
     * dropped rather than truncated, because a truncated set is not a landmark.
     */

    auto disjunctive_landmarks = std::vector<IndexList> {};
    for (const auto& record : records)
    {
        if (record.fact_atom_index.has_value() || record.member_atom_indices.size() < 2)
        {
            continue;
        }
        if (options.max_disjunctive_members > 0 && record.member_atom_indices.size() > options.max_disjunctive_members)
        {
            continue;
        }
        disjunctive_landmarks.push_back(record.member_atom_indices);
    }

    for (auto& edges : predecessors_by_atom)
    {
        sort_unique(edges);
    }
    for (auto& edges : successors_by_atom)
    {
        sort_unique(edges);
    }

    return FactLandmarkGraphImpl::create(problem,
                                         std::move(landmark_atom_indices),
                                         std::move(disjunctive_landmarks),
                                         std::move(predecessors_by_atom),
                                         std::move(successors_by_atom),
                                         std::move(records));
}

}
