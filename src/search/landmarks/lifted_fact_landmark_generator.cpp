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
            const auto predicate_index = atom->get_predicate()->get_index();
            m_static_atoms_by_predicate[predicate_index].push_back(atom);
            m_static_atom_identities.insert(make_identity(predicate_index, atom->get_objects()));
            for (size_t position = 0; position < atom->get_objects().size(); ++position)
            {
                m_static_atoms_by_slot[bucket_key(predicate_index, position, atom->get_objects()[position])].push_back(atom);
            }
        }
        for (const auto atom : problem->get_fluent_initial_atoms())
        {
            const auto predicate_index = atom->get_predicate()->get_index();
            m_initial_fluent_mask.set(atom->get_index());
            m_initial_fluent_identities.insert(make_identity(predicate_index, atom->get_objects()));
            m_fluent_initial_by_predicate[predicate_index].push_back(atom);
            for (size_t position = 0; position < atom->get_objects().size(); ++position)
            {
                m_fluent_initial_by_slot[bucket_key(predicate_index, position, atom->get_objects()[position])].push_back(atom);
            }
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

    bool has_static_atom(Index predicate_index, const ObjectList& objects) const
    {
        return m_static_atom_identities.count(make_identity(predicate_index, objects)) > 0;
    }

    /// @brief Is any of these fluent atoms true in the initial state?
    bool is_initially_true(const IndexList& atom_indices) const
    {
        return std::any_of(atom_indices.begin(), atom_indices.end(), [&](Index atom_index) { return m_initial_fluent_mask.get(atom_index); });
    }

    const GroundAtomList<FluentTag>& get_fluent_initial_atoms(Index predicate_index) const
    {
        static const auto empty = GroundAtomList<FluentTag> {};
        const auto it = m_fluent_initial_by_predicate.find(predicate_index);
        return (it == m_fluent_initial_by_predicate.end()) ? empty : it->second;
    }

    /// @brief The static atoms that can match `pattern`, which is the smallest bucket a bound
    /// position selects -- or the predicate's whole list when nothing is bound.
    ///
    /// Every atom matching the pattern agrees with it at every bound position, so it is in each of
    /// those positions' buckets; taking any one of them loses nothing, and taking the smallest is
    /// what turns a scan of sokoban's 10k-atom `move-dir` table into a scan of about four.
    const GroundAtomList<StaticTag>& get_static_atom_candidates(Index predicate_index, const ObjectList& pattern) const
    {
        return smallest_bucket(m_static_atoms_by_slot, get_static_atoms(predicate_index), predicate_index, pattern);
    }

    /// @brief The same, over the fluent initial atoms.
    const GroundAtomList<FluentTag>& get_fluent_initial_candidates(Index predicate_index, const ObjectList& pattern) const
    {
        return smallest_bucket(m_fluent_initial_by_slot, get_fluent_initial_atoms(predicate_index), predicate_index, pattern);
    }

    /// @brief Does the initial state contain any instance of the pattern `predicate(binding)`?
    ///
    /// The §2.7 gate. Deliberately *not* §2.1's test, which asks about the recorded members: an
    /// instance of the pattern that is not a member can hold at `I` without making the landmark
    /// trivially satisfied, and that is exactly the case §2.7's argument cannot survive.
    bool has_initial_pattern_instance(Index predicate_index, const ObjectList& binding) const
    {
        for (const auto atom : get_fluent_initial_candidates(predicate_index, binding))
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
                return true;
            }
        }
        return false;
    }

    /// @brief Is `predicate(objects)` a fluent initial atom?
    ///
    /// By identity rather than by index on purpose: this is asked about atoms
    /// that have not been interned yet, and interning them to ask would be the
    /// cost the question exists to avoid.
    bool has_initial_fluent_atom(Index predicate_index, const ObjectList& objects) const
    {
        return m_initial_fluent_identities.count(make_identity(predicate_index, objects)) > 0;
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
    /// @brief Key of the bucket holding every atom of `predicate_index` with `object` at `position`.
    static Identity bucket_key(Index predicate_index, size_t position, Object object)
    {
        return Identity { predicate_index, Index(position), object->get_index() };
    }

    /// @brief The smallest bucket a bound position of `pattern` selects, or `fallback`.
    template<typename AtomList>
    static const AtomList& smallest_bucket(const std::unordered_map<Identity, AtomList, IdentityHash>& buckets,
                                           const AtomList& fallback,
                                           Index predicate_index,
                                           const ObjectList& pattern)
    {
        const AtomList* best = nullptr;
        for (size_t position = 0; position < pattern.size(); ++position)
        {
            if (!pattern[position])
            {
                continue;
            }
            const auto it = buckets.find(bucket_key(predicate_index, position, pattern[position]));
            if (it == buckets.end())
            {
                static const auto empty = AtomList {};
                return empty;  // no atom has this object here, so none can match
            }
            if (!best || it->second.size() < best->size())
            {
                best = &it->second;
            }
        }
        return best ? *best : fallback;
    }

    ObjectList m_all_objects;
    std::unordered_map<Identity, GroundAtomList<StaticTag>, IdentityHash> m_static_atoms_by_slot;
    std::unordered_map<Identity, GroundAtomList<FluentTag>, IdentityHash> m_fluent_initial_by_slot;
    FlatBitset m_initial_fluent_mask;
    std::unordered_set<Identity, IdentityHash> m_initial_fluent_identities;
    std::unordered_map<Index, GroundAtomList<FluentTag>> m_fluent_initial_by_predicate;
    std::unordered_map<Index, GroundAtomList<StaticTag>> m_static_atoms_by_predicate;
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

/// @brief One way a schema adds a fluent predicate: the `(A, E)` pair and the effect literal.
///
/// `id` is a stable index over all adders of all predicates, so a unification result can be cached
/// on `(id, pattern)` -- the static filter that decides it is the expensive part of §2.7 and the
/// same pair recurs across thousands of expansions.
struct Adder
{
    const SchemaEffect* schema;
    Literal<FluentTag> effect_literal;
    Index id;
};

/// @brief Every schema effect that adds a given fluent predicate, indexed once.
///
/// The empty case is the one worth having: a predicate that *no* schema adds can only ever hold
/// where the initial state put it, and that is not a rarity -- mimir classifies a predicate as
/// fluent as soon as some effect DELETES it. miconic's `origin` is deleted by `board` and added by
/// nothing, so `origin(?, ?)` had 1.3 million typed instances of which all but the initial ones are
/// unreachable by construction.
std::unordered_map<Index, std::vector<Adder>> collect_adders_by_predicate(const std::vector<SchemaEffect>& schemas)
{
    auto adders = std::unordered_map<Index, std::vector<Adder>> {};
    auto next_id = Index(0);
    for (const auto& schema : schemas)
    {
        for (const auto effect_literal : schema.positive_fluent_effects)
        {
            adders[effect_literal->get_atom()->get_predicate()->get_index()].push_back(Adder { &schema, effect_literal, next_id++ });
        }
    }
    return adders;
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

    /* Slot-indexed scratch rather than a hash map per candidate atom. This is the inner loop of the
       whole extractor -- sokoban `test/p27-hard` reaches it about ten million times -- and a map
       allocated and freed per atom cost more than the matching it was there to record. Reset by
       walking the touched slots, so nothing is O(num_slots) per atom. */
    static thread_local auto local_object = ObjectList {};
    static thread_local auto local_slots = std::vector<Index> {};
    static thread_local auto narrowed = std::vector<ObjectList> {};
    static thread_local auto narrowed_slots = std::vector<Index> {};

    local_object.assign(candidates.size(), nullptr);
    narrowed.resize(candidates.size());
    for (const auto slot : narrowed_slots)
    {
        narrowed[slot].clear();
    }
    narrowed_slots.clear();

    /* Bound positions select the bucket; the loop below still checks every position, so this only
       decides how many atoms it has to look at. */
    auto bound_pattern = ObjectList {};
    bound_pattern.reserve(terms.size());
    for (const auto term : terms)
    {
        bound_pattern.push_back(resolve_term(term, sigma));
    }

    auto any_match = false;
    for (const auto atom : index.get_static_atom_candidates(predicate_index, bound_pattern))
    {
        const auto& objects = atom->get_objects();
        if (objects.size() != terms.size())
        {
            continue;
        }

        /* A variable may occur at several positions of one literal; the atom has to agree with
           itself there, which a per-position test alone would not catch. */
        local_slots.clear();
        auto consistent = true;
        for (size_t i = 0; consistent && i < terms.size(); ++i)
        {
            if (bound_pattern[i])
            {
                consistent = (bound_pattern[i] == objects[i]);
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
            if (local_object[slot])
            {
                consistent = (local_object[slot] == objects[i]);
            }
            else
            {
                local_object[slot] = objects[i];
                local_slots.push_back(slot);
            }
        }

        if (consistent)
        {
            any_match = true;
            for (const auto slot : local_slots)
            {
                if (narrowed[slot].empty())
                {
                    narrowed_slots.push_back(slot);
                }
                narrowed[slot].push_back(local_object[slot]);
            }
        }
        for (const auto slot : local_slots)
        {
            local_object[slot] = nullptr;
        }
    }

    if (!any_match)
    {
        return false;
    }

    for (const auto slot : narrowed_slots)
    {
        auto& objects = narrowed[slot];
        std::sort(objects.begin(), objects.end(), by_object_index);
        objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
        if (objects.empty())
        {
            return false;
        }
        changed |= (objects.size() < candidates[slot].size());
        candidates[slot] = objects;
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

/// @brief Unify one positive fluent effect literal with `binding` and, if asked, run the static
/// filter over the result: the `(A, E, sigma)` of §1, or nothing when the schema cannot produce it.
///
/// One function for two callers that ask the same question of different things. §2.2 asks it of a
/// partially bound pattern -- "which schemas could have achieved this subgoal" -- and §2.5 asks it
/// of a fully bound candidate member -- "could this atom ever be true". A `nullptr` in `binding`
/// means "free, constrains nothing", so a ground atom is just the case with no free positions.
std::optional<Achiever> try_build_achiever(const SchemaEffect& schema,
                                           Literal<FluentTag> effect_literal,
                                           const ObjectList& binding,
                                           const ProblemIndex& index,
                                           bool apply_filter)
{
    auto achiever = Achiever { &schema, ObjectList(schema.num_slots, nullptr), {} };

    const auto& terms = effect_literal->get_atom()->get_terms();
    if (terms.size() != binding.size())
    {
        return std::nullopt;
    }
    for (size_t i = 0; i < binding.size(); ++i)
    {
        if (!binding[i])
        {
            continue;  // a free position of the pattern constrains nothing
        }
        const auto& variant = terms[i]->get_variant();
        if (std::holds_alternative<Object>(variant))
        {
            if (std::get<Object>(variant) != binding[i])
            {
                return std::nullopt;
            }
            continue;
        }
        const auto slot = std::get<Variable>(variant)->get_parameter_index();
        // A repeated variable has to agree with itself, and the object has to be admissible for the
        // parameter -- the check that rules out spanner's `at(spanner1, ...)` against `walk`, whose
        // only `at` effect binds a `?m - man`.
        if ((achiever.sigma[slot] && achiever.sigma[slot] != binding[i])
            || !index.is_type_compatible(binding[i], schema.parameter_by_slot[slot]))
        {
            return std::nullopt;
        }
        achiever.sigma[slot] = binding[i];
    }

    achiever.candidates.resize(schema.num_slots);
    for (size_t slot = 0; slot < schema.num_slots; ++slot)
    {
        if (!achiever.sigma[slot])
        {
            achiever.candidates[slot] = index.get_candidate_objects(schema.parameter_by_slot[slot]);
        }
    }

    if (apply_filter && !apply_static_filter(achiever, index))
    {
        return std::nullopt;
    }
    return achiever;
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
        for (const auto atom : index.get_static_atom_candidates(predicate->get_index(), objects))
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

/// @brief Does every applicable ground instance of `adder` need an instance of `P(u)`?
///
/// True when some positive fluent precondition of the adder's schema is a `P(s)` whose terms, under
/// the adder's own substitution, already equal `u` on every position `u` binds. Free positions of
/// `u` impose nothing, so this is exactly "every ground instance of that precondition lies in
/// inst(P(u))".
bool adder_needs_pattern(const SchemaEffect& schema,
                         const ObjectList& sigma,
                         Predicate<FluentTag> pattern_predicate,
                         const ObjectList& pattern_binding)
{
    for (const auto literal : schema.positive_fluent_preconditions)
    {
        const auto atom = literal->get_atom();
        if (atom->get_predicate() != pattern_predicate || atom->get_terms().size() != pattern_binding.size())
        {
            continue;
        }
        const auto& terms = atom->get_terms();
        auto covers = true;
        for (size_t i = 0; covers && i < pattern_binding.size(); ++i)
        {
            covers = !pattern_binding[i] || (resolve_term(terms[i], sigma) == pattern_binding[i]);
        }
        if (covers)
        {
            return true;
        }
    }
    return false;
}

/// @brief `try_build_achiever` for one adder against one precondition pattern, memoized.
///
/// The substitution is all §2.7 needs from the achiever, and it is a pure function of the adder and
/// the pattern -- independent of which landmark is being expanded -- so one cache serves the whole
/// extraction. Without it, rovers `test/p30-hard` re-runs the static filter over 46k `visible_from`
/// atoms for the same pairs across 1,600 expansions: 91 s against 28 s.
using AdderSigmaCache = std::unordered_map<Identity, std::optional<ObjectList>, IdentityHash>;

const std::optional<ObjectList>&
adder_substitution(const Adder& adder, const ObjectList& pattern, const ProblemIndex& index, AdderSigmaCache& cache)
{
    auto key = make_identity(adder.id, pattern);
    const auto cached = cache.find(key);
    if (cached != cache.end())
    {
        return cached->second;
    }
    const auto achiever = try_build_achiever(*adder.schema, adder.effect_literal, pattern, index, true);
    auto sigma = achiever.has_value() ? std::make_optional(achiever->sigma) : std::nullopt;
    return cache.emplace(std::move(key), std::move(sigma)).first->second;
}

/// @brief §2.7, the self-dependent-precondition rule. Narrows `achiever`, or returns false to drop
/// it because it cannot be the *first* achiever of `P(u)`.
///
/// Richter-Helmert-Westphal exclude an achiever that the RPG cannot reach without already having
/// the landmark. This is that exclusion at schema level, against the initial state instead of an
/// RPG, and it is what keeps the back-chain alive on blocksworld: expanding `clear(b)`, the
/// achievers are `unstack(?x, b)`, `stack(b, ?y)` and `putdown(b)`, the last two need `holding(b)`,
/// and every adder of `holding(b)` needs `clear(b)` itself -- so neither can be first, but the
/// intersection over all three is empty and the chain dies where a landmark was waiting.
///
/// Caller must have checked the gate: no instance of the *pattern* is true in `I`. That is stricter
/// than §2.1's member-level stop and cannot be folded into it -- a non-member instance of the
/// pattern holding at `I` is harmless for §2.1 and fatal here, because the argument below turns on
/// there being no instance at all before the first achiever fires.
bool apply_self_dependent_precondition_rule(Achiever& achiever,
                                            Predicate<FluentTag> pattern_predicate,
                                            const ObjectList& pattern_binding,
                                            const ProblemIndex& index,
                                            const std::unordered_map<Index, std::vector<Adder>>& adders_by_predicate,
                                            std::unordered_map<Identity, bool, IdentityHash>& decided,
                                            AdderSigmaCache& adder_cache,
                                            bool& changed)
{
    for (const auto literal : achiever.schema->positive_fluent_preconditions)
    {
        const auto predicate = literal->get_atom()->get_predicate();
        const auto& terms = literal->get_atom()->get_terms();

        auto precondition_pattern = ObjectList {};
        precondition_pattern.reserve(terms.size());
        for (const auto term : terms)
        {
            precondition_pattern.push_back(resolve_term(term, achiever.sigma));
        }

        /* Vacuously true when nothing adds the predicate at all -- miconic's `origin`, where this
           rule alone fixes the passenger's floor.

           Cached on the precondition pattern: `P(u)` is fixed for this expansion, so the answer
           depends only on `Q(v)`, and the same `Q(v)` recurs across the achievers and across the
           fixpoint's iterations. */
        auto every_adder_needs_pattern = true;
        auto identity = make_identity(predicate->get_index(), precondition_pattern);
        const auto cached = decided.find(identity);
        if (cached != decided.end())
        {
            every_adder_needs_pattern = cached->second;
        }
        else
        {
            const auto adders = adders_by_predicate.find(predicate->get_index());
            if (adders != adders_by_predicate.end())
            {
                for (const auto& adder : adders->second)
                {
                    const auto& sigma = adder_substitution(adder, precondition_pattern, index, adder_cache);
                    if (!sigma.has_value())
                    {
                        continue;  // cannot produce this precondition at all, so it is not an adder of it
                    }
                    if (!adder_needs_pattern(*adder.schema, *sigma, pattern_predicate, pattern_binding))
                    {
                        every_adder_needs_pattern = false;
                        break;
                    }
                }
            }
            decided.emplace(std::move(identity), every_adder_needs_pattern);
        }
        if (!every_adder_needs_pattern)
        {
            continue;
        }

        /* Every way of producing this precondition would already need `P(u)`, so the instance the
           first achiever uses cannot have been produced: it is an initial atom. */
        auto narrowed = std::unordered_map<Index, ObjectList> {};
        auto any_match = false;
        for (const auto atom : index.get_fluent_initial_candidates(predicate->get_index(), precondition_pattern))
        {
            const auto& objects = atom->get_objects();
            if (objects.size() != terms.size())
            {
                continue;
            }
            auto local = std::unordered_map<Index, Object> {};
            auto consistent = true;
            for (size_t i = 0; consistent && i < terms.size(); ++i)
            {
                if (precondition_pattern[i])
                {
                    consistent = (precondition_pattern[i] == objects[i]);
                    continue;
                }
                const auto slot = term_slot(terms[i]);
                if (slot == FREE_POSITION || slot >= achiever.candidates.size())
                {
                    continue;
                }
                const auto& domain = achiever.candidates[slot];
                if (!std::binary_search(domain.begin(), domain.end(), objects[i], by_object_index))
                {
                    consistent = false;
                    break;
                }
                const auto [position, inserted] = local.emplace(slot, objects[i]);
                consistent = inserted || (position->second == objects[i]);
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
            return false;  // no initial instance it could have used: it cannot be the first achiever
        }

        for (auto& [slot, objects] : narrowed)
        {
            std::sort(objects.begin(), objects.end(), by_object_index);
            objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
            if (objects.empty())
            {
                return false;
            }
            changed |= (objects.size() < achiever.candidates[slot].size());
            achiever.candidates[slot] = std::move(objects);
        }
    }

    for (size_t slot = 0; slot < achiever.candidates.size(); ++slot)
    {
        if (!achiever.sigma[slot] && achiever.candidates[slot].size() == 1)
        {
            achiever.sigma[slot] = achiever.candidates[slot].front();
            changed = true;
        }
    }
    return true;
}

/// @brief §2.5's reachability test for one candidate member, memoized by identity.
///
/// A ground atom that is not a fluent initial atom and that no statically consistent,
/// type-compatible instance of any schema effect adds is false in *every* reachable state. Dropping
/// it from a member set therefore preserves the landmark property exactly: "every plan makes some
/// member true" quantifies over reachable states, and this atom is in none of them, so it was never
/// one of the members a plan could have made true.
///
/// This is the one filter §2.5 originally left out ("do not add delete-relaxed reachability to
/// members ... measured, not fixed, in this iteration"). What was measured is that it is not a
/// precision nicety: without it, `origin(?, ?)` on miconic `test` carries 1,314,565 members that no
/// plan can reach, LIW's rank set triples, and every one of those atoms is interned into the
/// problem's repositories. Note what this is NOT: it is not delete-relaxed reachability, which
/// would need the ground action universe this generator exists to avoid. It asks only whether a
/// schema can produce the atom at all -- a purely lifted question, answered once per identity.
class MemberReachability
{
public:
    MemberReachability(const ProblemIndex& index, const std::unordered_map<Index, std::vector<Adder>>& adders_by_predicate) :
        m_index(index),
        m_adders_by_predicate(adders_by_predicate)
    {
    }

    bool can_ever_hold(Predicate<FluentTag> predicate, const ObjectList& objects)
    {
        const auto predicate_index = predicate->get_index();
        if (m_index.has_initial_fluent_atom(predicate_index, objects))
        {
            return true;
        }

        const auto it = m_adders_by_predicate.find(predicate_index);
        if (it == m_adders_by_predicate.end())
        {
            // No schema adds this predicate at all, so the initial state is the only source and the
            // test above already answered. Short-circuited before the memo, because this is the
            // case that would otherwise cost a million lookups.
            return false;
        }

        auto identity = make_identity(predicate_index, objects);
        const auto cached = m_memo.find(identity);
        if (cached != m_memo.end())
        {
            return cached->second;
        }

        auto reachable = false;
        for (const auto& adder : it->second)
        {
            // The static filter always runs here, whatever `use_static_filter` says: that option
            // governs which achievers the *intersection* of §2.3 runs over, while this is the
            // member definition of §2.5, whose static consistency test was never optional either.
            if (try_build_achiever(*adder.schema, adder.effect_literal, objects, m_index, true).has_value())
            {
                reachable = true;
                break;
            }
        }
        m_memo.emplace(std::move(identity), reachable);
        return reachable;
    }

private:
    const ProblemIndex& m_index;
    const std::unordered_map<Index, std::vector<Adder>>& m_adders_by_predicate;
    std::unordered_map<Identity, bool, IdentityHash> m_memo;
};

}

FactLandmarkGraph LiftedFactLandmarkGenerator::create(const Problem& problem, const LiftedFactLandmarkGeneratorOptions& options)
{
    const auto index = ProblemIndex(problem);
    const auto schemas = collect_schema_effects(problem);
    const auto adders_by_predicate = collect_adders_by_predicate(schemas);
    auto reachability = MemberReachability(index, adders_by_predicate);

    static const auto no_adders = std::vector<Adder> {};
    const auto adders_of = [&](Predicate<FluentTag> predicate) -> const std::vector<Adder>&
    {
        const auto it = adders_by_predicate.find(predicate->get_index());
        return (it == adders_by_predicate.end()) ? no_adders : it->second;
    };

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
            if (members.size() == 1)
            {
                /* Every plan makes *some* member true and there is exactly one, so that member is
                   mandatory -- a theorem, not a policy, which is why there is no option to skip it.
                   The partial form is dropped rather than kept alongside: expanding it too would
                   rederive, more weakly, everything the promoted fact derives. */
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
    auto derived_predicate_indices = IndexList {};
    auto member_objects = ObjectList {};
    auto sdp_decisions = std::unordered_map<Identity, bool, IdentityHash> {};
    auto adder_cache = AdderSigmaCache {};

    while (!worklist.empty())
    {
        const auto position = worklist.front();
        worklist.pop_front();

        const auto predicate = records[position].predicate;
        const auto binding = records[position].binding;  // by value: `records` reallocates below

        /**
         * §2.1: nothing may be derived through a landmark that already holds at `I`. The induction
         * step of Proposition 1 needs "the first state containing a MEMBER was produced by an
         * action", so the test is over the recorded member set, not over the pattern: an instance
         * of the pattern that is true in `I` but is not a member (the static filter excluded it --
         * a rover parked at a waypoint it is not equipped to analyse) is no way to satisfy the
         * landmark we recorded, and stopping on it would lose real landmarks for nothing.
         *
         * The landmark itself stays (trivially satisfied at I, parity with the grounded generator,
         * whose back-chain stops at the same place).
         */

        if (index.is_initially_true(records[position].member_atom_indices))
        {
            records[position].initially_true = true;
            continue;
        }

        /**
         * §2.2 + §2.3: the achievers of the pattern, statically filtered.
         */

        achievers.clear();
        for (const auto& adder : adders_of(predicate))
        {
            auto achiever = try_build_achiever(*adder.schema, adder.effect_literal, binding, index, options.use_static_filter);
            if (achiever.has_value())
            {
                achievers.push_back(std::move(*achiever));
            }
        }

        /**
         * §2.7: drop the achievers that cannot be the *first* one, and narrow those that can.
         *
         * Gated on the pattern, not on the members: the argument needs "no instance of `P(u)` is
         * true before the first achiever fires", and a non-member instance holding at `I` breaks
         * it while leaving §2.1's member-level stop untouched.
         */
        if (!index.has_initial_pattern_instance(predicate->get_index(), binding))
        {
            sdp_decisions.clear();
            auto surviving = std::vector<Achiever> {};
            for (auto& achiever : achievers)
            {
                auto keep = true;
                // A binding the rule discovers can collapse another precondition's adder set, and
                // the static filter can bind more still, so the two run to a joint fixpoint.
                for (auto changed = true; keep && changed;)
                {
                    changed = false;
                    keep = apply_self_dependent_precondition_rule(achiever, predicate, binding, index, adders_by_predicate, sdp_decisions, adder_cache, changed);
                    if (keep && changed)
                    {
                        keep = apply_static_filter(achiever, index);
                    }
                }
                if (keep)
                {
                    surviving.push_back(std::move(achiever));
                }
            }
            achievers = std::move(surviving);
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

        /* Ascending predicate index, never the hash order of `occurrences_by_achiever.front()`.
           §2.4 subsumption keeps a general pattern only when no more specific one was inserted
           before it, so the order in which predicates are visited decides which records survive --
           and an unordered_map's order differs between libc++ and libstdc++, which would make the
           member vocabulary a property of the machine that built it. */
        derived_predicate_indices.clear();
        for (const auto& [precondition_predicate_index, _occurrences] : occurrences_by_achiever.front())
        {
            derived_predicate_indices.push_back(precondition_predicate_index);
        }
        std::sort(derived_predicate_indices.begin(), derived_predicate_indices.end());

        for (const auto precondition_predicate_index : derived_predicate_indices)
        {
            auto occurrences = std::vector<const LiteralList<FluentTag>*> { &occurrences_by_achiever.front().at(precondition_predicate_index) };
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
                                member_objects.clear();
                                member_objects.reserve(terms.size());
                                for (const auto term : terms)
                                {
                                    member_objects.push_back(resolve_term(term, completion));
                                }
                                /* Asked BEFORE interning, not after: the atoms this rejects are the
                                   ones whose interning was the cost, so testing the identity rather
                                   than the interned atom is what actually keeps them out of the
                                   problem's repositories. */
                                if (reachability.can_ever_hold(precondition_predicate, member_objects))
                                {
                                    members.push_back(
                                        problem->get_or_create_ground_atom<FluentTag>(precondition_predicate, member_objects)->get_index());
                                }
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

    /* The flag is a property of the *recorded* set, and a record's set can still grow after it was
       expanded (another parent deriving the same identity contributes its own members, which are
       unioned in). Recomputing here rather than trusting what the expansion loop saw is what keeps
       the flag from going stale under that union. */
    for (auto& record : records)
    {
        record.initially_true = index.is_initially_true(record.member_atom_indices);
    }

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
