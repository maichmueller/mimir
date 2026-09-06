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

#include "mimir/search/relaxed_reachability.hpp"

#include "mimir/formalism/action.hpp"
#include "mimir/formalism/atom.hpp"
#include "mimir/formalism/axiom.hpp"
#include "mimir/formalism/conjunctive_condition.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/literal.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/parameter.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/formalism/tags.hpp"
#include "mimir/formalism/term.hpp"
#include "mimir/formalism/type.hpp"
#include "mimir/formalism/variable.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace mimir::formalism;

namespace mimir::search::relaxed_reachability
{

using ObjectId = uint32_t;
using RelationId = uint32_t;
using VariableId = uint32_t;

constexpr RelationId NO_RELATION = std::numeric_limits<RelationId>::max();
constexpr uint32_t NO_INDEX = std::numeric_limits<uint32_t>::max();
constexpr ObjectId NO_OBJECT = std::numeric_limits<ObjectId>::max();
constexpr VariableId NO_VARIABLE = std::numeric_limits<VariableId>::max();

/// @brief mimir compiles PDDL equality into an ordinary static predicate named "=", but it does not always
/// materialise its atoms (`data/childsnack` has one per object, an IPC childsnack instance has none). Its
/// semantics are object identity, so it is evaluated here rather than looked up: a positive `=` merges the
/// two terms at compile time, a negative one becomes a disequality guard.
static bool is_equality_predicate(Predicate<StaticTag> predicate) { return predicate->get_name() == "="; }

/**
 * Relation: an append-only set of fixed-arity object tuples.
 *
 * Tuples live in one flat vector in insertion order, which is what makes semi-naive evaluation free of
 * separate delta relations: the tuples a round added are exactly the suffix behind the size the round
 * started with. The open-addressing table on the side deduplicates on insert.
 */
class Relation
{
private:
    uint32_t m_arity = 0;
    size_t m_size = 0;
    std::vector<ObjectId> m_data;
    std::vector<uint32_t> m_buckets;  ///< tuple position + 1, 0 = empty

    static uint64_t hash(const ObjectId* values, uint32_t arity)
    {
        auto h = uint64_t(1469598103934665603ull);
        for (uint32_t i = 0; i < arity; ++i)
        {
            h ^= uint64_t(values[i]);
            h *= uint64_t(1099511628211ull);
        }
        h ^= h >> 29;
        h *= uint64_t(0xbf58476d1ce4e5b9ull);
        h ^= h >> 32;
        return h;
    }

    void rehash(size_t min_capacity)
    {
        auto capacity = size_t(16);
        while (capacity < min_capacity * 2)
        {
            capacity *= 2;
        }
        m_buckets.assign(capacity, 0);
        const auto mask = capacity - 1;
        for (size_t position = 0; position < m_size; ++position)
        {
            auto slot = size_t(hash(m_data.data() + position * m_arity, m_arity) & mask);
            while (m_buckets[slot] != 0)
            {
                slot = (slot + 1) & mask;
            }
            m_buckets[slot] = uint32_t(position + 1);
        }
    }

public:
    Relation() = default;
    explicit Relation(uint32_t arity) : m_arity(arity) {}

    uint32_t get_arity() const { return m_arity; }
    size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    const ObjectId* get_tuple(size_t position) const { return m_data.data() + position * size_t(m_arity); }

    bool insert(const ObjectId* values)
    {
        if (m_arity == 0)
        {
            // The empty tuple: the relation is a truth value.
            if (m_size != 0)
            {
                return false;
            }
            m_size = 1;
            return true;
        }
        if (m_buckets.empty() || (m_size + 1) * 4 >= m_buckets.size() * 3)
        {
            rehash(m_size + 1);
        }
        const auto mask = m_buckets.size() - 1;
        auto slot = size_t(hash(values, m_arity) & mask);
        while (m_buckets[slot] != 0)
        {
            const auto* other = m_data.data() + size_t(m_buckets[slot] - 1) * m_arity;
            if (std::memcmp(other, values, m_arity * sizeof(ObjectId)) == 0)
            {
                return false;
            }
            slot = (slot + 1) & mask;
        }
        m_buckets[slot] = uint32_t(m_size + 1);
        m_data.insert(m_data.end(), values, values + m_arity);
        ++m_size;
        return true;
    }

    bool contains(const ObjectId* values) const
    {
        if (m_arity == 0)
        {
            return m_size != 0;
        }
        if (m_buckets.empty())
        {
            return false;
        }
        const auto mask = m_buckets.size() - 1;
        auto slot = size_t(hash(values, m_arity) & mask);
        while (m_buckets[slot] != 0)
        {
            const auto* other = m_data.data() + size_t(m_buckets[slot] - 1) * m_arity;
            if (std::memcmp(other, values, m_arity * sizeof(ObjectId)) == 0)
            {
                return true;
            }
            slot = (slot + 1) & mask;
        }
        return false;
    }

    /// @brief Pre-size for `num_tuples`. A restricted query derives a subset of the unrestricted fixpoint, so
    /// the sizes that run measured are exact upper bounds and every regrow can be skipped.
    void reserve(size_t num_tuples)
    {
        if (m_arity == 0 || num_tuples == 0)
        {
            return;
        }
        m_data.reserve(num_tuples * m_arity);
        auto capacity = std::max(size_t(16), m_buckets.size());
        while (num_tuples * 2 >= capacity)
        {
            capacity *= 2;
        }
        if (capacity != m_buckets.size())
        {
            m_buckets.assign(capacity, 0);
        }
    }

    void reset(uint32_t arity)
    {
        m_arity = arity;
        m_size = 0;
        m_data.clear();
        m_buckets.assign(m_buckets.size(), 0);
    }

    size_t get_memory_bytes() const { return m_data.capacity() * sizeof(ObjectId) + m_buckets.capacity() * sizeof(uint32_t); }
};

/// @brief How many distinct objects occur in each column of `relation`.
///
/// This is the only number the join planner really wants: the fanout of a join on a column is the relation's
/// size divided by the number of distinct keys in it, and the size of a projection is bounded by the product
/// of the distinct values of the surviving columns. Counting from the declared types instead -- which is all
/// that is available before the first fixpoint -- over-estimates badly: sokoban's `adjacent(?l1, ?l2, ?dir)`
/// has four directions in its third column, not `|objects|`.
static std::vector<double> compute_position_domains(const Relation& relation)
{
    auto result = std::vector<double>(relation.get_arity(), 0.0);
    auto seen = std::vector<std::unordered_set<ObjectId>>(relation.get_arity());
    for (size_t position = 0; position < relation.size(); ++position)
    {
        const auto* tuple = relation.get_tuple(position);
        for (uint32_t column = 0; column < relation.get_arity(); ++column)
        {
            seen[column].insert(tuple[column]);
        }
    }
    for (uint32_t column = 0; column < relation.get_arity(); ++column)
    {
        result[column] = double(seen[column].size());
    }
    return result;
}

/**
 * JoinIndex: a hash index over a column subset of an append-only relation.
 *
 * `refresh` only consumes the tuples appended since the last call, so an index over a growing IDB relation
 * costs one bucket write per new tuple rather than a rebuild -- except when the table has to grow, which is
 * amortised doubling.
 */
class JoinIndex
{
private:
    std::vector<uint32_t> m_columns;
    std::vector<uint32_t> m_buckets;  ///< head tuple position + 1, 0 = empty
    std::vector<uint32_t> m_next;     ///< per tuple position, next position + 1 in the same bucket
    size_t m_num_indexed = 0;

    static uint64_t hash(const ObjectId* values, size_t count)
    {
        auto h = uint64_t(1469598103934665603ull);
        for (size_t i = 0; i < count; ++i)
        {
            h ^= uint64_t(values[i]);
            h *= uint64_t(1099511628211ull);
        }
        h ^= h >> 29;
        h *= uint64_t(0xbf58476d1ce4e5b9ull);
        h ^= h >> 32;
        return h;
    }

    uint64_t hash_columns(const Relation& relation, size_t position) const
    {
        const auto* tuple = relation.get_tuple(position);
        auto h = uint64_t(1469598103934665603ull);
        for (const auto column : m_columns)
        {
            h ^= uint64_t(tuple[column]);
            h *= uint64_t(1099511628211ull);
        }
        h ^= h >> 29;
        h *= uint64_t(0xbf58476d1ce4e5b9ull);
        h ^= h >> 32;
        return h;
    }

public:
    void set_columns(std::vector<uint32_t> columns)
    {
        m_columns = std::move(columns);
        clear();
    }

    const std::vector<uint32_t>& get_columns() const { return m_columns; }

    void clear()
    {
        m_buckets.clear();
        m_next.clear();
        m_num_indexed = 0;
    }

    void refresh(const Relation& relation)
    {
        if (m_num_indexed == relation.size())
        {
            return;
        }
        auto capacity = std::max(size_t(16), m_buckets.size());
        while (relation.size() * 2 >= capacity)
        {
            capacity *= 2;
        }
        if (capacity != m_buckets.size())
        {
            m_buckets.assign(capacity, 0);
            m_num_indexed = 0;  ///< the bucket count changed, so every tuple has to be placed again
        }
        m_next.resize(relation.size(), 0);
        const auto mask = m_buckets.size() - 1;
        for (size_t position = m_num_indexed; position < relation.size(); ++position)
        {
            const auto slot = size_t(hash_columns(relation, position) & mask);
            m_next[position] = m_buckets[slot];
            m_buckets[slot] = uint32_t(position + 1);
        }
        m_num_indexed = relation.size();
    }

    /// @brief Call `visit(position)` for every tuple whose indexed columns equal `key`.
    template<typename Visitor>
    void for_each_match(const Relation& relation, const ObjectId* key, Visitor&& visit) const
    {
        if (m_buckets.empty())
        {
            return;
        }
        const auto mask = m_buckets.size() - 1;
        const auto slot = size_t(hash(key, m_columns.size()) & mask);
        for (auto entry = m_buckets[slot]; entry != 0; entry = m_next[entry - 1])
        {
            const auto position = size_t(entry - 1);
            const auto* tuple = relation.get_tuple(position);
            auto matches = true;
            for (size_t i = 0; i < m_columns.size(); ++i)
            {
                if (tuple[m_columns[i]] != key[i])
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
            {
                visit(position);
            }
        }
    }

    size_t get_memory_bytes() const { return m_buckets.capacity() * sizeof(uint32_t) + m_next.capacity() * sizeof(uint32_t); }
};

/**
 * The compiled program
 */

enum class RelationKind : uint8_t
{
    True,        ///< arity 0, holds the empty tuple; the left input of every rule's first join
    Static,      ///< EDB, from `get_static_initial_atoms()`
    TypeDomain,  ///< EDB, the objects of one declared type list; only used to make a rule safe
    Fluent,      ///< IDB
    Derived,     ///< IDB, produced by axioms
    Auxiliary    ///< IDB, an intermediate of rule splitting
};

/// @brief Where one value of a join step comes from: a column of the left input, a column of the right
/// input, or a constant.
struct Slot
{
    uint8_t side;  ///< 0 = left, 1 = right, 2 = constant
    uint32_t value;
};

struct Guard
{
    enum class Kind : uint8_t
    {
        NotInStatic,  ///< the tuple built from `args` must be absent from the static relation `relation`
        NotEqual      ///< `args[0]` and `args[1]` must denote different objects
    };

    Kind kind;
    RelationId relation = NO_RELATION;
    std::vector<Slot> args;
};

struct Step
{
    RelationId out = NO_RELATION;
    RelationId lhs = NO_RELATION;
    RelationId rhs = NO_RELATION;  ///< NO_RELATION for the projection-only step of a body-less rule
    bool rhs_is_edb = false;

    std::vector<uint32_t> join_lhs_columns;  ///< left columns carrying the join key
    std::vector<uint32_t> join_rhs_columns;  ///< the right columns they are matched against
    std::vector<std::pair<uint32_t, ObjectId>> const_rhs_columns;
    std::vector<std::pair<uint32_t, uint32_t>> self_eq_rhs_columns;  ///< (later occurrence, first occurrence)

    std::vector<Guard> guards;
    std::vector<Slot> out_columns;

    uint32_t rhs_index = NO_INDEX;  ///< index over `join_rhs_columns ++ const columns`; EDB or IDB by `rhs_is_edb`
    uint32_t lhs_index = NO_INDEX;  ///< index over `join_lhs_columns`, only when the right side can still grow
};

/// @brief One ground atom named without interning it: the relation plus the object tuple.
struct RelationTuple
{
    RelationId relation;
    std::vector<ObjectId> values;
};

class Program
{
public:
    Problem problem;
    RelaxedReachabilityOptions options;

    /* Objects */
    ObjectList object_by_id;
    std::unordered_map<Object, ObjectId> id_by_object;

    /* Relations */
    std::vector<RelationKind> relation_kind;
    std::vector<uint32_t> relation_arity;
    std::vector<const void*> relation_predicate;  ///< the predicate a relation stands for, nullptr otherwise
    std::vector<std::string> relation_name;       ///< for diagnostics
    std::vector<double> relation_size_estimate;   ///< the join planner's cost model
    std::vector<std::vector<double>> relation_position_domain;
    std::unordered_map<const void*, RelationId> relation_by_predicate;

    RelationId true_relation = NO_RELATION;

    /* EDB, shared by every query: filled once, never modified again. */
    std::vector<Relation> edb;                ///< indexed by relation id; only EDB slots are populated
    std::vector<JoinIndex> edb_indexes;
    size_t num_edb_index_slots = 0;

    /* Initial fluent atoms, as relation tuples, so a restricted query can drop some of them. */
    std::vector<RelationTuple> initial_fluent_atoms;

    /* Plan */
    std::vector<Step> steps;
    std::unordered_map<std::string, RelationId> shared_auxiliary_steps;  ///< identical prefixes are computed once
    std::vector<size_t> relation_capacity_hint;                          ///< sizes the unrestricted fixpoint measured
    size_t num_idb_index_slots = 0;
    size_t num_auxiliary_relations = 0;

    /* Goal */
    bool static_goal_holds = true;
    std::vector<RelationTuple> goal_tuples;

    /* Statistics collected while compiling. */
    size_t num_rules = 0;
    size_t num_dropped_rules = 0;

    ObjectId get_object_id(Object object) const
    {
        const auto it = id_by_object.find(object);
        return (it == id_by_object.end()) ? NO_OBJECT : it->second;
    }

    RelationId get_relation_id(const void* predicate) const
    {
        const auto it = relation_by_predicate.find(predicate);
        return (it == relation_by_predicate.end()) ? NO_RELATION : it->second;
    }
};

/**
 * Database: the relations one fixpoint derives.
 */
class Database
{
private:
    /// @brief How far one step has already looked into each of its two inputs.
    ///
    /// The delta of an append-only relation is the suffix behind the position the step last saw, so a step
    /// covers exactly `[0, now_lhs) x [0, now_rhs)` minus `[0, seen_lhs) x [0, seen_rhs)` -- no pair twice and
    /// none missed. Tracking this per *step* rather than per round matters: a relation can grow in the middle
    /// of a round, and a round-global boundary would then mark tuples as old for a step that never saw them.
    struct StepState
    {
        size_t seen_lhs = 0;
        size_t seen_rhs = 0;
    };

    const Program* m_program;
    std::vector<Relation> m_idb;
    std::vector<JoinIndex> m_indexes;
    std::vector<StepState> m_step_state;
    std::vector<const Relation*> m_forbidden;  ///< per relation, the atoms a restricted query removed
    std::vector<Relation> m_forbidden_storage;
    size_t m_num_rounds = 0;

    /* Reusable scratch, so a join never allocates. */
    std::vector<ObjectId> m_probe_key;
    std::vector<ObjectId> m_out_tuple;
    std::vector<ObjectId> m_guard_tuple;

public:
    explicit Database(const Program& program) :
        m_program(&program),
        m_idb(program.relation_kind.size()),
        m_indexes(program.num_idb_index_slots),
        m_step_state(program.steps.size()),
        m_forbidden(program.relation_kind.size(), nullptr),
        m_forbidden_storage(program.relation_kind.size())
    {
        for (RelationId id = 0; id < program.relation_kind.size(); ++id)
        {
            if (program.relation_kind[id] != RelationKind::Static && program.relation_kind[id] != RelationKind::TypeDomain
                && program.relation_kind[id] != RelationKind::True)
            {
                m_idb[id] = Relation(program.relation_arity[id]);
                if (id < program.relation_capacity_hint.size())
                {
                    m_idb[id].reserve(program.relation_capacity_hint[id]);
                }
            }
        }
        for (const auto& step : program.steps)
        {
            if (step.rhs_index != NO_INDEX && !step.rhs_is_edb)
            {
                auto columns = step.join_rhs_columns;
                for (const auto& [column, value] : step.const_rhs_columns)
                {
                    columns.push_back(column);
                }
                m_indexes[step.rhs_index].set_columns(std::move(columns));
            }
            if (step.lhs_index != NO_INDEX)
            {
                m_indexes[step.lhs_index].set_columns(step.join_lhs_columns);
            }
        }
    }

    const Program& get_program() const { return *m_program; }

    const Relation& get_relation(RelationId id) const
    {
        const auto kind = m_program->relation_kind[id];
        return (kind == RelationKind::Static || kind == RelationKind::TypeDomain || kind == RelationKind::True) ? m_program->edb[id] : m_idb[id];
    }

    size_t get_num_rounds() const { return m_num_rounds; }

    void set_forbidden(const std::vector<RelationTuple>& forbidden)
    {
        for (const auto& atom : forbidden)
        {
            auto& storage = m_forbidden_storage[atom.relation];
            if (m_forbidden[atom.relation] == nullptr)
            {
                storage.reset(m_program->relation_arity[atom.relation]);
                m_forbidden[atom.relation] = &storage;
            }
            storage.insert(atom.values.data());
        }
    }

    bool is_forbidden(RelationId id, const ObjectId* values) const
    {
        const auto* forbidden = m_forbidden[id];
        return forbidden != nullptr && forbidden->contains(values);
    }

    /// @brief Seed the fluent relations with the initial state minus the forbidden atoms.
    void seed_initial_atoms()
    {
        for (const auto& atom : m_program->initial_fluent_atoms)
        {
            if (!is_forbidden(atom.relation, atom.values.data()))
            {
                m_idb[atom.relation].insert(atom.values.data());
            }
        }
    }

    bool is_goal_reached() const
    {
        if (!m_program->static_goal_holds)
        {
            return false;
        }
        for (const auto& atom : m_program->goal_tuples)
        {
            if (!get_relation(atom.relation).contains(atom.values.data()))
            {
                return false;
            }
        }
        return true;
    }

    /// @brief Run the semi-naive fixpoint. Stops early once the goal is reached when `stop_at_goal`.
    void run(bool stop_at_goal)
    {
        seed_initial_atoms();

        if (stop_at_goal && is_goal_reached())
        {
            return;
        }

        auto changed = true;
        while (changed)
        {
            changed = false;
            ++m_num_rounds;

            for (size_t index = 0; index < m_program->steps.size(); ++index)
            {
                changed |= run_step(m_program->steps[index], m_step_state[index]);
            }

            if (stop_at_goal && is_goal_reached())
            {
                return;
            }
        }
    }

    size_t get_num_tuples(RelationKind kind) const
    {
        auto total = size_t(0);
        for (RelationId id = 0; id < m_program->relation_kind.size(); ++id)
        {
            if (m_program->relation_kind[id] == kind)
            {
                total += get_relation(id).size();
            }
        }
        return total;
    }

    size_t get_memory_bytes() const
    {
        auto total = size_t(0);
        for (const auto& relation : m_idb)
        {
            total += relation.get_memory_bytes();
        }
        for (const auto& index : m_indexes)
        {
            total += index.get_memory_bytes();
        }
        return total;
    }

private:
    ObjectId read_slot(const Slot& slot, const ObjectId* lhs_tuple, const ObjectId* rhs_tuple) const
    {
        switch (slot.side)
        {
            case 0: return lhs_tuple[slot.value];
            case 1: return rhs_tuple[slot.value];
            default: return ObjectId(slot.value);
        }
    }

    bool guards_hold(const Step& step, const ObjectId* lhs_tuple, const ObjectId* rhs_tuple)
    {
        for (const auto& guard : step.guards)
        {
            if (guard.kind == Guard::Kind::NotEqual)
            {
                if (read_slot(guard.args[0], lhs_tuple, rhs_tuple) == read_slot(guard.args[1], lhs_tuple, rhs_tuple))
                {
                    return false;
                }
            }
            else
            {
                m_guard_tuple.clear();
                for (const auto& arg : guard.args)
                {
                    m_guard_tuple.push_back(read_slot(arg, lhs_tuple, rhs_tuple));
                }
                if (get_relation(guard.relation).contains(m_guard_tuple.data()))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool emit(const Step& step, const ObjectId* lhs_tuple, const ObjectId* rhs_tuple)
    {
        if (!guards_hold(step, lhs_tuple, rhs_tuple))
        {
            return false;
        }
        m_out_tuple.clear();
        for (const auto& slot : step.out_columns)
        {
            m_out_tuple.push_back(read_slot(slot, lhs_tuple, rhs_tuple));
        }
        if (is_forbidden(step.out, m_out_tuple.data()))
        {
            return false;
        }
        return m_idb[step.out].insert(m_out_tuple.data());
    }

    bool rhs_tuple_matches_filters(const Step& step, const ObjectId* rhs_tuple) const
    {
        for (const auto& [column, value] : step.const_rhs_columns)
        {
            if (rhs_tuple[column] != value)
            {
                return false;
            }
        }
        for (const auto& [later, first] : step.self_eq_rhs_columns)
        {
            if (rhs_tuple[later] != rhs_tuple[first])
            {
                return false;
            }
        }
        return true;
    }

    bool run_step(const Step& step, StepState& state)
    {
        const auto& lhs = get_relation(step.lhs);
        const auto previous_lhs_size = state.seen_lhs;
        // A recursive rule can have its own head on the right, so the right side may grow while the step runs.
        // Fixing the bound up front keeps the step finite and keeps it from consuming its own output twice.
        const auto lhs_size = lhs.size();

        if (step.rhs == NO_RELATION)
        {
            auto changed = false;
            for (auto position = previous_lhs_size; position < lhs_size; ++position)
            {
                changed |= emit(step, lhs.get_tuple(position), nullptr);
            }
            state.seen_lhs = lhs_size;
            return changed;
        }

        const auto& rhs = get_relation(step.rhs);
        const auto previous_rhs_size = state.seen_rhs;
        const auto rhs_size = rhs.size();
        state.seen_lhs = lhs_size;
        state.seen_rhs = rhs_size;
        auto changed = false;

        /* Part A: the left tuples this step has not seen yet, against the whole right side. */
        if (previous_lhs_size < lhs_size && rhs_size > 0)
        {
            if (step.rhs_index != NO_INDEX)
            {
                // An EDB index is built once in `compile_plan` and shared by every query; only an IDB index
                // has to catch up with the tuples the last rounds appended.
                const JoinIndex* index = nullptr;
                if (step.rhs_is_edb)
                {
                    index = &m_program->edb_indexes[step.rhs_index];
                }
                else
                {
                    m_indexes[step.rhs_index].refresh(rhs);
                    index = &m_indexes[step.rhs_index];
                }
                m_probe_key.resize(step.join_rhs_columns.size() + step.const_rhs_columns.size());
                for (size_t i = 0; i < step.const_rhs_columns.size(); ++i)
                {
                    m_probe_key[step.join_rhs_columns.size() + i] = step.const_rhs_columns[i].second;
                }
                for (auto position = previous_lhs_size; position < lhs_size; ++position)
                {
                    const auto* lhs_tuple = lhs.get_tuple(position);
                    for (size_t i = 0; i < step.join_lhs_columns.size(); ++i)
                    {
                        m_probe_key[i] = lhs_tuple[step.join_lhs_columns[i]];
                    }
                    index->for_each_match(rhs,
                                          m_probe_key.data(),
                                          [&](size_t rhs_position)
                                          {
                                              const auto* rhs_tuple = rhs.get_tuple(rhs_position);
                                              if (rhs_tuple_matches_filters(step, rhs_tuple))
                                              {
                                                  changed |= emit(step, lhs_tuple, rhs_tuple);
                                              }
                                          });
                }
            }
            else
            {
                for (auto position = previous_lhs_size; position < lhs_size; ++position)
                {
                    const auto* lhs_tuple = lhs.get_tuple(position);
                    for (size_t rhs_position = 0; rhs_position < rhs_size; ++rhs_position)
                    {
                        const auto* rhs_tuple = rhs.get_tuple(rhs_position);
                        if (rhs_tuple_matches_filters(step, rhs_tuple))
                        {
                            changed |= emit(step, lhs_tuple, rhs_tuple);
                        }
                    }
                }
            }
        }

        /* Part B: the right tuples this step has not seen yet, against the left tuples it already has. An EDB
           right side never grows, so this half costs nothing there. The right tuple is re-read inside the loop
           because a recursive rule may append to the very relation being scanned. */
        if (previous_rhs_size < rhs_size && previous_lhs_size > 0)
        {
            if (step.lhs_index != NO_INDEX)
            {
                auto& index = m_indexes[step.lhs_index];
                index.refresh(lhs);
                m_probe_key.resize(step.join_rhs_columns.size());
                for (auto rhs_position = previous_rhs_size; rhs_position < rhs_size; ++rhs_position)
                {
                    if (!rhs_tuple_matches_filters(step, rhs.get_tuple(rhs_position)))
                    {
                        continue;
                    }
                    for (size_t i = 0; i < step.join_rhs_columns.size(); ++i)
                    {
                        m_probe_key[i] = rhs.get_tuple(rhs_position)[step.join_rhs_columns[i]];
                    }
                    index.for_each_match(lhs,
                                         m_probe_key.data(),
                                         [&](size_t lhs_position)
                                         {
                                             if (lhs_position < previous_lhs_size)
                                             {
                                                 changed |= emit(step, lhs.get_tuple(lhs_position), rhs.get_tuple(rhs_position));
                                             }
                                         });
                }
            }
            else
            {
                for (auto rhs_position = previous_rhs_size; rhs_position < rhs_size; ++rhs_position)
                {
                    if (!rhs_tuple_matches_filters(step, rhs.get_tuple(rhs_position)))
                    {
                        continue;
                    }
                    for (size_t position = 0; position < previous_lhs_size; ++position)
                    {
                        changed |= emit(step, lhs.get_tuple(position), rhs.get_tuple(rhs_position));
                    }
                }
            }
        }

        return changed;
    }
};

/**
 * Rule construction
 */

struct RuleTerm
{
    bool is_variable = false;
    uint32_t value = 0;  ///< variable id or object id
};

struct RuleAtom
{
    RelationId relation = NO_RELATION;
    std::vector<RuleTerm> args;
};

struct Rule
{
    RuleAtom head;
    std::vector<RuleAtom> body;             ///< positive literals, all of them safe-making
    std::vector<RuleAtom> negative_static;  ///< anti-joins against the static EDB
    std::vector<std::pair<RuleTerm, RuleTerm>> disequalities;
    uint32_t num_variables = 0;
    std::vector<double> variable_domain;  ///< per variable, the number of type-compatible objects
    std::string provenance;
};

/// @brief Everything the compiler needs while turning schemas into rules.
class Compiler
{
public:
    Program& program;

    explicit Compiler(Program& p) : program(p) {}

    std::map<std::vector<const TypeImpl*>, RelationId> type_domain_relations;
    std::map<std::vector<const TypeImpl*>, double> type_domain_sizes;

    /// @brief How many objects a parameter of these types can take. Memoised: it is asked once per predicate
    /// position and once per rule variable, and a linear scan over 1629 objects each time adds up.
    double count_type_compatible_objects(const TypeList& types)
    {
        auto key = std::vector<const TypeImpl*>(types.begin(), types.end());
        const auto it = type_domain_sizes.find(key);
        if (it != type_domain_sizes.end())
        {
            return it->second;
        }
        auto count = double(0);
        for (const auto object : program.object_by_id)
        {
            if (is_subtypeeq(object->get_bases(), types))
            {
                ++count;
            }
        }
        type_domain_sizes.emplace(std::move(key), count);
        return count;
    }

    RelationId get_or_create_type_domain(const TypeList& types)
    {
        auto key = std::vector<const TypeImpl*>(types.begin(), types.end());
        const auto it = type_domain_relations.find(key);
        if (it != type_domain_relations.end())
        {
            return it->second;
        }
        const auto id = RelationId(program.relation_kind.size());
        program.relation_kind.push_back(RelationKind::TypeDomain);
        program.relation_arity.push_back(1);
        program.relation_predicate.push_back(nullptr);
        program.relation_name.push_back("$type_domain_" + std::to_string(type_domain_relations.size()));
        program.relation_position_domain.push_back({ 0.0 });
        program.relation_size_estimate.push_back(0.0);
        program.edb.emplace_back(1);
        auto& relation = program.edb.back();
        for (ObjectId object_id = 0; object_id < program.object_by_id.size(); ++object_id)
        {
            if (is_subtypeeq(program.object_by_id[object_id]->get_bases(), types))
            {
                relation.insert(&object_id);
            }
        }
        program.relation_size_estimate[id] = double(relation.size());
        program.relation_position_domain[id][0] = double(relation.size());
        type_domain_relations.emplace(std::move(key), id);
        return id;
    }
};

/**
 * Join ordering and rule splitting
 */

struct PendingGuard
{
    Guard::Kind kind;
    RelationId relation = NO_RELATION;
    std::vector<RuleTerm> args;
};

static void collect_variables(const std::vector<RuleTerm>& terms, std::vector<char>& mask)
{
    for (const auto& term : terms)
    {
        if (term.is_variable)
        {
            mask[term.value] = 1;
        }
    }
}

/// @brief A canonical description of one auxiliary-producing step, so that an identical step of another rule
/// can reuse the relation it computes.
static std::string describe_step(const Step& step, const std::vector<VariableId>& out_variables)
{
    auto stream = std::ostringstream {};
    stream << step.lhs << '|' << step.rhs << '|';
    for (const auto column : step.join_lhs_columns)
        stream << column << ',';
    stream << '|';
    for (const auto column : step.join_rhs_columns)
        stream << column << ',';
    stream << '|';
    for (const auto& [column, value] : step.const_rhs_columns)
        stream << column << ':' << value << ',';
    stream << '|';
    for (const auto& [later, first] : step.self_eq_rhs_columns)
        stream << later << ':' << first << ',';
    stream << '|';
    for (const auto& guard : step.guards)
    {
        stream << int(guard.kind) << ':' << guard.relation << ':';
        for (const auto& arg : guard.args)
            stream << int(arg.side) << '.' << arg.value << ',';
        stream << ';';
    }
    stream << '|';
    for (const auto& slot : step.out_columns)
        stream << int(slot.side) << '.' << slot.value << ',';
    stream << '|';
    for (const auto variable : out_variables)
        stream << variable << ',';
    return stream.str();
}

/// @brief Split one rule into a chain of binary joins, each projecting away every variable that neither the
/// head nor any not-yet-joined literal or guard still needs.
///
/// The greedy order minimises, per step, the number of candidate pairs the join has to look at and then the
/// number of tuples it materialises. That is what turns childsnack's `make_sandwich` from
/// |breads| x |contents| x |sandwiches| into their sum: `?b` occurs only in `at_kitchen_bread(?b)` and in the
/// type literal `bread-portion(?b)`, so after those two are joined `?b` is dead and the accumulator drops
/// back to the empty tuple.
static double compile_rule(Program& program, const Rule& rule, std::vector<Step>& out_steps, size_t seed_literal, bool dry_run)
{
    const auto num_variables = rule.num_variables;
    auto total_cost = double(0);

    auto head_variables = std::vector<char>(num_variables, 0);
    collect_variables(rule.head.args, head_variables);

    auto pending_guards = std::vector<PendingGuard> {};
    for (const auto& atom : rule.negative_static)
    {
        pending_guards.push_back(PendingGuard { Guard::Kind::NotInStatic, atom.relation, atom.args });
    }
    for (const auto& [lhs, rhs] : rule.disequalities)
    {
        pending_guards.push_back(PendingGuard { Guard::Kind::NotEqual, NO_RELATION, { lhs, rhs } });
    }

    auto remaining = std::vector<size_t> {};
    for (size_t i = 0; i < rule.body.size(); ++i)
    {
        remaining.push_back(i);
    }

    /* A variable ranges over at most the objects that actually occur at the positions it is joined on, which
       is usually far fewer than its declared type allows. Taking the tightest of those bounds is what keeps
       the projection estimate from collapsing into "the same as the probe count" for every candidate. */
    auto variable_domain = rule.variable_domain;
    for (const auto& literal : rule.body)
    {
        const auto& position_domain = program.relation_position_domain[literal.relation];
        for (size_t position = 0; position < literal.args.size(); ++position)
        {
            const auto& term = literal.args[position];
            if (term.is_variable && position_domain[position] > 0.0)
            {
                variable_domain[term.value] = std::min(variable_domain[term.value], position_domain[position]);
            }
        }
    }

    auto accumulator_relation = program.true_relation;
    auto accumulator_variables = std::vector<VariableId> {};  ///< in column order
    auto accumulator_column = std::vector<uint32_t>(num_variables, NO_INDEX);
    auto accumulator_estimate = double(1);

    if (remaining.empty())
    {
        if (dry_run)
        {
            return 0.0;
        }
        // Every literal was decided at compile time: the head is a fact, gated only on guards over constants.
        auto step = Step {};
        step.out = rule.head.relation;
        step.lhs = accumulator_relation;
        step.rhs = NO_RELATION;
        for (const auto& guard : pending_guards)
        {
            auto compiled = Guard { guard.kind, guard.relation, {} };
            for (const auto& term : guard.args)
            {
                compiled.args.push_back(Slot { 2, term.value });
            }
            step.guards.push_back(std::move(compiled));
        }
        for (const auto& term : rule.head.args)
        {
            step.out_columns.push_back(Slot { 2, term.value });
        }
        out_steps.push_back(std::move(step));
        return 0.0;
    }

    auto is_first_step = true;

    while (!remaining.empty())
    {
        auto best_slot = std::numeric_limits<size_t>::max();
        auto best_pairs = std::numeric_limits<double>::infinity();
        auto best_output = std::numeric_limits<double>::infinity();
        auto best_connected = false;

        for (size_t slot = 0; slot < remaining.size(); ++slot)
        {
            /* The first literal is dictated by the caller. A greedy chain is only ever as good as where it
               starts, and the cheapest single relation is often the wrong door: childsnack's
               `serve_sandwich_no_gluten` seeded at `place` (4 tuples) is dragged through `at` and `waiting`
               into a child-by-sandwich intermediate of 34k rows, whereas seeded at `no_gluten_sandwich` it
               joins `ontray`, drops `?s` on the spot and never exceeds the eight trays.
               `compile_rule_with_best_seed` plans once per possible first literal and keeps the cheapest. */
            if (is_first_step && seed_literal < rule.body.size() && remaining[slot] != seed_literal)
            {
                continue;
            }

            const auto& literal = rule.body[remaining[slot]];
            const auto relation = literal.relation;
            const auto& position_domain = program.relation_position_domain[relation];

            auto selectivity = double(1);
            auto key_domain = double(1);
            auto num_key_columns = size_t(0);
            auto seen = std::vector<char>(num_variables, 0);
            const auto connected = std::any_of(literal.args.begin(),
                                               literal.args.end(),
                                               [&](const RuleTerm& term)
                                               { return term.is_variable && accumulator_column[term.value] != NO_INDEX; });

            for (size_t position = 0; position < literal.args.size(); ++position)
            {
                const auto& term = literal.args[position];
                const auto domain = std::max(1.0, position_domain[position]);
                if (!term.is_variable)
                {
                    selectivity /= domain;
                }
                else if (seen[term.value])
                {
                    selectivity /= domain;
                }
                else
                {
                    seen[term.value] = 1;
                    if (accumulator_column[term.value] != NO_INDEX)
                    {
                        key_domain *= domain;
                        ++num_key_columns;
                    }
                }
            }

            const auto effective = std::max(1.0, program.relation_size_estimate[relation] * selectivity);
            const auto fanout = (num_key_columns == 0) ? effective : std::max(1.0, effective / std::max(1.0, key_domain));
            const auto pairs = accumulator_estimate * fanout;

            /* What survives the projection after this literal. */
            auto needed = std::vector<char>(head_variables);
            for (size_t other = 0; other < remaining.size(); ++other)
            {
                if (other != slot)
                {
                    collect_variables(rule.body[remaining[other]].args, needed);
                }
            }
            auto bound = std::vector<char>(num_variables, 0);
            for (const auto variable : accumulator_variables)
            {
                bound[variable] = 1;
            }
            collect_variables(literal.args, bound);
            for (const auto& guard : pending_guards)
            {
                auto evaluable = true;
                for (const auto& term : guard.args)
                {
                    if (term.is_variable && !bound[term.value])
                    {
                        evaluable = false;
                        break;
                    }
                }
                if (!evaluable)
                {
                    collect_variables(guard.args, needed);
                }
            }

            auto output = double(1);
            for (VariableId variable = 0; variable < num_variables; ++variable)
            {
                if (bound[variable] && needed[variable])
                {
                    output *= std::max(1.0, variable_domain[variable]);
                }
            }
            output = std::min(output, std::max(1.0, pairs));

            /* A literal sharing a variable with the accumulator always wins over one that does not.
               Without that rule the greedy is seduced by the smallest relation in the body -- sokoban's
               `(:constants down up left right - direction)` gives `direction(?dir)` four tuples, so a pure
               cost comparison picks it, then `box(?b)`, and by the time the two `adjacent` literals are
               reached every intermediate is a cross product. Among connected literals (or among all of them
               when none is connected, which is the case for the very first one) the smallest materialised
               intermediate wins, with the probe count as the tie-break: the cost of the rest of the chain is
               driven by how many tuples this step leaves behind, not by how many it looked at. */
            const auto better = (best_slot == std::numeric_limits<size_t>::max())                                       //
                                || (connected && !best_connected)                                                      //
                                || (connected == best_connected                                                        //
                                    && (output < best_output || (output == best_output && pairs < best_pairs)));
            if (better)
            {
                best_slot = slot;
                best_pairs = pairs;
                best_output = output;
                best_connected = connected;
            }
        }
        total_cost += best_pairs + best_output;

        const auto literal_index = remaining[best_slot];
        remaining.erase(remaining.begin() + best_slot);
        is_first_step = false;
        const auto& literal = rule.body[literal_index];

        auto emit_this_step = !dry_run;
        auto step = Step {};
        step.lhs = accumulator_relation;
        step.rhs = literal.relation;
        const auto rhs_kind = program.relation_kind[literal.relation];
        step.rhs_is_edb = (rhs_kind == RelationKind::Static || rhs_kind == RelationKind::TypeDomain || rhs_kind == RelationKind::True);

        auto rhs_column = std::vector<uint32_t>(num_variables, NO_INDEX);
        for (uint32_t position = 0; position < literal.args.size(); ++position)
        {
            const auto& term = literal.args[position];
            if (!term.is_variable)
            {
                step.const_rhs_columns.emplace_back(position, ObjectId(term.value));
            }
            else if (rhs_column[term.value] != NO_INDEX)
            {
                step.self_eq_rhs_columns.emplace_back(position, rhs_column[term.value]);
            }
            else
            {
                rhs_column[term.value] = position;
                if (accumulator_column[term.value] != NO_INDEX)
                {
                    step.join_lhs_columns.push_back(accumulator_column[term.value]);
                    step.join_rhs_columns.push_back(position);
                }
            }
        }

        /* Which guards become evaluable here. */
        auto bound = std::vector<char>(num_variables, 0);
        for (const auto variable : accumulator_variables)
        {
            bound[variable] = 1;
        }
        for (VariableId variable = 0; variable < num_variables; ++variable)
        {
            if (rhs_column[variable] != NO_INDEX)
            {
                bound[variable] = 1;
            }
        }

        const auto to_slot = [&](const RuleTerm& term)
        {
            if (!term.is_variable)
            {
                return Slot { 2, term.value };
            }
            if (accumulator_column[term.value] != NO_INDEX)
            {
                return Slot { 0, accumulator_column[term.value] };
            }
            return Slot { 1, rhs_column[term.value] };
        };

        auto still_pending = std::vector<PendingGuard> {};
        for (const auto& guard : pending_guards)
        {
            auto evaluable = true;
            for (const auto& term : guard.args)
            {
                if (term.is_variable && !bound[term.value])
                {
                    evaluable = false;
                    break;
                }
            }
            if (!evaluable)
            {
                still_pending.push_back(guard);
                continue;
            }
            auto compiled = Guard { guard.kind, guard.relation, {} };
            for (const auto& term : guard.args)
            {
                compiled.args.push_back(to_slot(term));
            }
            step.guards.push_back(std::move(compiled));
        }
        pending_guards = std::move(still_pending);

        if (remaining.empty())
        {
            /* The last join writes the head directly; no extra projection relation is needed. */
            step.out = rule.head.relation;
            for (const auto& term : rule.head.args)
            {
                step.out_columns.push_back(to_slot(term));
            }
        }
        else
        {
            auto needed = std::vector<char>(head_variables);
            for (const auto other : remaining)
            {
                collect_variables(rule.body[other].args, needed);
            }
            for (const auto& guard : pending_guards)
            {
                collect_variables(guard.args, needed);
            }

            auto out_variables = std::vector<VariableId> {};
            for (const auto variable : accumulator_variables)
            {
                if (needed[variable])
                {
                    out_variables.push_back(variable);
                    step.out_columns.push_back(Slot { 0, accumulator_column[variable] });
                }
            }
            for (uint32_t position = 0; position < literal.args.size(); ++position)
            {
                const auto& term = literal.args[position];
                if (term.is_variable && rhs_column[term.value] == position && accumulator_column[term.value] == NO_INDEX && needed[term.value])
                {
                    out_variables.push_back(term.value);
                    step.out_columns.push_back(Slot { 1, position });
                }
            }

            /* Two rules over the same body -- sokoban's `push` has three positive effect literals, so three
               rules share every precondition -- compile to the same prefix as long as they still need the
               same variables. Emitting that prefix once is a plain saving in both time and tuples. */
            auto next_estimate = best_output;
            if (dry_run)
            {
                accumulator_relation = program.true_relation;  ///< a trial plan allocates nothing
            }
            else
            {
                const auto key = describe_step(step, out_variables);
                const auto it = program.shared_auxiliary_steps.find(key);
                if (it != program.shared_auxiliary_steps.end())
                {
                    accumulator_relation = it->second;
                    next_estimate = program.relation_size_estimate[accumulator_relation];
                    emit_this_step = false;
                }
                else
                {
                    const auto id = RelationId(program.relation_kind.size());
                    program.relation_kind.push_back(RelationKind::Auxiliary);
                    program.relation_arity.push_back(uint32_t(out_variables.size()));
                    program.relation_predicate.push_back(nullptr);
                    program.relation_name.push_back("$aux_" + std::to_string(program.num_auxiliary_relations));
                    program.relation_position_domain.push_back(std::vector<double>(out_variables.size(), 0.0));
                    program.relation_size_estimate.push_back(best_output);
                    program.edb.emplace_back();
                    ++program.num_auxiliary_relations;
                    step.out = id;
                    program.shared_auxiliary_steps.emplace(key, id);
                    accumulator_relation = id;
                }
            }

            accumulator_variables = out_variables;
            std::fill(accumulator_column.begin(), accumulator_column.end(), NO_INDEX);
            for (uint32_t column = 0; column < out_variables.size(); ++column)
            {
                accumulator_column[out_variables[column]] = column;
            }
            accumulator_estimate = next_estimate;
        }

        if (!emit_this_step)
        {
            continue;
        }

        /* Index slots. The right side is probed whenever there is anything to probe on; the left side is
           probed only when the right side can still grow, i.e. when it is an IDB relation. */
        if (!step.join_rhs_columns.empty() || !step.const_rhs_columns.empty())
        {
            if (step.rhs_is_edb)
            {
                step.rhs_index = uint32_t(program.num_edb_index_slots++);
            }
            else
            {
                step.rhs_index = uint32_t(program.num_idb_index_slots++);
            }
        }
        if (!step.rhs_is_edb && !step.join_lhs_columns.empty())
        {
            step.lhs_index = uint32_t(program.num_idb_index_slots++);
        }

        out_steps.push_back(std::move(step));
    }

    return total_cost;
}

/// @brief Plan one rule from every possible first literal and emit the cheapest chain.
///
/// A trial plan allocates nothing, so this costs |body| passes of an O(|body|^2) loop per rule and no memory.
/// Measured on the thirteen largest test instances it never added more than a few milliseconds to the
/// compilation, and it is what stops one unlucky opening literal from turning a linear chain into a product.
static void compile_rule_with_best_seed(Program& program, const Rule& rule, std::vector<Step>& out_steps)
{
    auto best_seed = std::numeric_limits<size_t>::max();
    auto best_cost = std::numeric_limits<double>::infinity();
    auto scratch = std::vector<Step> {};
    for (size_t seed = 0; seed < rule.body.size(); ++seed)
    {
        scratch.clear();
        const auto cost = compile_rule(program, rule, scratch, seed, true);
        if (cost < best_cost)
        {
            best_cost = cost;
            best_seed = seed;
        }
    }
    compile_rule(program, rule, out_steps, best_seed, false);
}

/**
 * Reading the schemas
 */

/// @brief Assemble the rules of one conjunctive condition plus one head literal into a `Rule`.
///
/// `slot_of_variable` is `Variable::get_parameter_index()`: mimir lays the binding out as the action's own
/// parameters followed by the conditional effect's, and the parameters report exactly that index, so the
/// layout is read off the model instead of assumed.
class RuleBuilder
{
private:
    Program& m_program;
    Compiler& m_compiler;

public:
    RuleBuilder(Program& program, Compiler& compiler) : m_program(program), m_compiler(compiler) {}

    static VariableId find(std::vector<VariableId>& parent, VariableId slot)
    {
        while (parent[slot] != slot)
        {
            parent[slot] = parent[parent[slot]];
            slot = parent[slot];
        }
        return slot;
    }

    /// @brief Build the rule for one head literal over one condition. Returns false when the condition is
    /// statically unsatisfiable, in which case the rule is dropped.
    template<typename HeadPredicate>
    bool build(HeadPredicate head_predicate,
               const TermList& head_terms,
               const std::vector<ConjunctiveCondition>& conditions,
               const ParameterList& parameter_by_slot,
               std::string provenance,
               Rule& out_rule)
    {
        const auto num_slots = uint32_t(parameter_by_slot.size());

        /* Merge slots joined by a positive `=`, and bind slots a positive `=` pins to a constant. */
        auto parent = std::vector<VariableId>(num_slots);
        for (VariableId slot = 0; slot < num_slots; ++slot)
        {
            parent[slot] = slot;
        }
        auto constant_of_slot = std::vector<ObjectId>(num_slots, NO_OBJECT);

        const auto term_slot = [](Term term) -> VariableId
        {
            const auto& variant = term->get_variant();
            return std::holds_alternative<Variable>(variant) ? VariableId(std::get<Variable>(variant)->get_parameter_index()) : NO_VARIABLE;
        };
        const auto term_object = [this](Term term) -> ObjectId
        {
            const auto& variant = term->get_variant();
            return std::holds_alternative<Object>(variant) ? m_program.get_object_id(std::get<Object>(variant)) : NO_OBJECT;
        };

        for (const auto condition : conditions)
        {
            for (const auto literal : condition->get_literals<StaticTag>())
            {
                if (!literal->get_polarity() || !is_equality_predicate(literal->get_atom()->get_predicate()))
                {
                    continue;
                }
                const auto& terms = literal->get_atom()->get_terms();
                if (terms.size() != 2)
                {
                    continue;
                }
                const auto lhs_slot = term_slot(terms[0]);
                const auto rhs_slot = term_slot(terms[1]);
                if (lhs_slot != NO_VARIABLE && rhs_slot != NO_VARIABLE)
                {
                    parent[find(parent, lhs_slot)] = find(parent, rhs_slot);
                }
                else if (lhs_slot != NO_VARIABLE)
                {
                    constant_of_slot[find(parent, lhs_slot)] = term_object(terms[1]);
                }
                else if (rhs_slot != NO_VARIABLE)
                {
                    constant_of_slot[find(parent, rhs_slot)] = term_object(terms[0]);
                }
                else if (term_object(terms[0]) != term_object(terms[1]))
                {
                    return false;  ///< `(= a b)` for two different constants
                }
            }
        }
        /* Propagate the constants to the representatives; a conflict makes the condition unsatisfiable. */
        for (VariableId slot = 0; slot < num_slots; ++slot)
        {
            if (constant_of_slot[slot] == NO_OBJECT)
            {
                continue;
            }
            const auto root = find(parent, slot);
            if (constant_of_slot[root] != NO_OBJECT && constant_of_slot[root] != constant_of_slot[slot])
            {
                return false;
            }
            constant_of_slot[root] = constant_of_slot[slot];
        }

        /* Dense variable ids for the slots that stayed variables. */
        auto variable_of_root = std::vector<VariableId>(num_slots, NO_VARIABLE);
        auto variable_domain = std::vector<double> {};
        auto num_variables = uint32_t(0);
        const auto to_term = [&](Term term) -> RuleTerm
        {
            const auto& variant = term->get_variant();
            if (std::holds_alternative<Object>(variant))
            {
                return RuleTerm { false, m_program.get_object_id(std::get<Object>(variant)) };
            }
            const auto root = find(parent, VariableId(std::get<Variable>(variant)->get_parameter_index()));
            if (constant_of_slot[root] != NO_OBJECT)
            {
                return RuleTerm { false, constant_of_slot[root] };
            }
            if (variable_of_root[root] == NO_VARIABLE)
            {
                variable_of_root[root] = num_variables++;
                const auto parameter = (root < parameter_by_slot.size()) ? parameter_by_slot[root] : nullptr;
                variable_domain.push_back(parameter ? m_compiler.count_type_compatible_objects(parameter->get_bases()) :
                                                      double(m_program.object_by_id.size()));
            }
            return RuleTerm { true, variable_of_root[root] };
        };

        out_rule = Rule {};
        out_rule.provenance = std::move(provenance);

        /* Head. */
        out_rule.head.relation = m_program.get_relation_id(head_predicate);
        for (const auto term : head_terms)
        {
            out_rule.head.args.push_back(to_term(term));
        }

        /* Body. */
        const auto add_positive = [&](RelationId relation, const TermList& terms)
        {
            auto atom = RuleAtom {};
            atom.relation = relation;
            for (const auto term : terms)
            {
                atom.args.push_back(to_term(term));
            }
            out_rule.body.push_back(std::move(atom));
        };

        for (const auto condition : conditions)
        {
            for (const auto literal : condition->get_literals<StaticTag>())
            {
                const auto predicate = literal->get_atom()->get_predicate();
                if (is_equality_predicate(predicate))
                {
                    if (!literal->get_polarity() && m_program.options.enforce_negative_static_conditions)
                    {
                        const auto& terms = literal->get_atom()->get_terms();
                        if (terms.size() == 2)
                        {
                            const auto lhs = to_term(terms[0]);
                            const auto rhs = to_term(terms[1]);
                            if (!lhs.is_variable && !rhs.is_variable)
                            {
                                if (lhs.value == rhs.value)
                                {
                                    return false;  ///< `(not (= a a))` can never hold
                                }
                            }
                            else
                            {
                                out_rule.disequalities.emplace_back(lhs, rhs);
                            }
                        }
                    }
                    continue;  ///< the positive case was already folded into the substitution
                }

                const auto relation = m_program.get_relation_id(predicate);
                if (literal->get_polarity())
                {
                    add_positive(relation, literal->get_atom()->get_terms());
                }
                else if (m_program.options.enforce_negative_static_conditions)
                {
                    auto atom = RuleAtom {};
                    atom.relation = relation;
                    for (const auto term : literal->get_atom()->get_terms())
                    {
                        atom.args.push_back(to_term(term));
                    }
                    out_rule.negative_static.push_back(std::move(atom));
                }
            }
            /* Positive fluent and derived literals join; the negative ones are what the delete relaxation
               throws away (see the class comment). */
            for (const auto literal : condition->get_literals<FluentTag>())
            {
                if (literal->get_polarity())
                {
                    add_positive(m_program.get_relation_id(literal->get_atom()->get_predicate()), literal->get_atom()->get_terms());
                }
            }
            for (const auto literal : condition->get_literals<DerivedTag>())
            {
                if (literal->get_polarity())
                {
                    add_positive(m_program.get_relation_id(literal->get_atom()->get_predicate()), literal->get_atom()->get_terms());
                }
            }
        }

        out_rule.num_variables = num_variables;
        out_rule.variable_domain = std::move(variable_domain);

        /* Safety: every variable of the head and of a guard must be bound by a positive body literal. A
           variable that occurs nowhere else still ranges over its declared type -- an action parameter that
           appears only in a negative precondition is still universally instantiated by the grounder -- so the
           missing binding is supplied by the type domain rather than by dropping the rule. */
        auto bound = std::vector<char>(num_variables, 0);
        for (const auto& atom : out_rule.body)
        {
            collect_variables(atom.args, bound);
        }
        const auto require_bound = [&](const std::vector<RuleTerm>& terms)
        {
            for (const auto& term : terms)
            {
                if (!term.is_variable || bound[term.value])
                {
                    continue;
                }
                VariableId root = NO_VARIABLE;
                for (VariableId slot = 0; slot < num_slots; ++slot)
                {
                    if (variable_of_root[slot] == term.value)
                    {
                        root = slot;
                        break;
                    }
                }
                const auto parameter = (root != NO_VARIABLE && root < parameter_by_slot.size()) ? parameter_by_slot[root] : nullptr;
                const auto relation = m_compiler.get_or_create_type_domain(parameter ? parameter->get_bases() : TypeList {});
                auto atom = RuleAtom {};
                atom.relation = relation;
                atom.args.push_back(term);
                out_rule.body.push_back(std::move(atom));
                bound[term.value] = 1;
            }
        };
        require_bound(out_rule.head.args);
        for (const auto& atom : out_rule.negative_static)
        {
            require_bound(atom.args);
        }
        for (const auto& [lhs, rhs] : out_rule.disequalities)
        {
            require_bound({ lhs, rhs });
        }

        return true;
    }
};

/**
 * Program construction
 */

static RelationId register_relation(Program& program, RelationKind kind, uint32_t arity, const void* predicate, std::string name)
{
    const auto id = RelationId(program.relation_kind.size());
    program.relation_kind.push_back(kind);
    program.relation_arity.push_back(arity);
    program.relation_predicate.push_back(predicate);
    program.relation_name.push_back(std::move(name));
    program.relation_position_domain.emplace_back(arity, 0.0);
    program.relation_size_estimate.push_back(1.0);
    program.edb.emplace_back(arity);
    if (predicate != nullptr)
    {
        program.relation_by_predicate.emplace(predicate, id);
    }
    return id;
}

template<IsStaticOrFluentOrDerivedTag P>
static void register_predicates(Program& program, Compiler& compiler, const PredicateList<P>& predicates, RelationKind kind)
{
    for (const auto predicate : predicates)
    {
        if constexpr (std::is_same_v<P, StaticTag>)
        {
            // `=` is evaluated on object identity rather than looked up, so it never becomes a relation.
            if (is_equality_predicate(predicate))
            {
                continue;
            }
        }
        if (program.relation_by_predicate.count(predicate) > 0)
        {
            continue;
        }
        const auto id = register_relation(program, kind, uint32_t(predicate->get_arity()), predicate, predicate->get_name());
        auto product = double(1);
        for (size_t position = 0; position < predicate->get_arity(); ++position)
        {
            const auto domain = compiler.count_type_compatible_objects(predicate->get_parameters()[position]->get_bases());
            program.relation_position_domain[id][position] = domain;
            product *= std::max(1.0, domain);
        }
        program.relation_size_estimate[id] = product;
    }
}

static ParameterList collect_parameter_by_slot(const std::vector<ParameterList>& parameter_lists)
{
    auto result = ParameterList {};
    for (const auto& parameters : parameter_lists)
    {
        for (const auto parameter : parameters)
        {
            const auto slot = size_t(parameter->get_variable()->get_parameter_index());
            if (slot >= result.size())
            {
                result.resize(slot + 1, nullptr);
            }
            result[slot] = parameter;
        }
    }
    return result;
}

static std::vector<Rule> build_rules(Program& program, Compiler& compiler)
{
    auto rules = std::vector<Rule> {};
    auto builder = RuleBuilder(program, compiler);

    for (const auto action : program.problem->get_domain()->get_actions())
    {
        for (const auto effect : action->get_conditional_effects())
        {
            const auto parameter_by_slot = collect_parameter_by_slot({ action->get_conjunctive_condition()->get_parameters(),
                                                                      effect->get_conjunctive_condition()->get_parameters(),
                                                                      effect->get_conjunctive_effect()->get_parameters() });
            const auto conditions = std::vector<ConjunctiveCondition> { action->get_conjunctive_condition(), effect->get_conjunctive_condition() };

            for (const auto effect_literal : effect->get_conjunctive_effect()->get_literals())
            {
                if (!effect_literal->get_polarity())
                {
                    continue;  ///< delete effects are what the relaxation removes
                }
                ++program.num_rules;
                auto rule = Rule {};
                if (!builder.build(effect_literal->get_atom()->get_predicate(),
                                   effect_literal->get_atom()->get_terms(),
                                   conditions,
                                   parameter_by_slot,
                                   action->get_name(),
                                   rule))
                {
                    ++program.num_dropped_rules;
                    continue;
                }
                rules.push_back(std::move(rule));
            }
        }
    }

    for (const auto axiom : program.problem->get_problem_and_domain_axioms())
    {
        const auto literal = axiom->get_literal();
        if (!literal->get_polarity())
        {
            continue;  ///< an axiom deriving a negated atom has no meaning in a monotone fixpoint
        }
        const auto parameter_by_slot = collect_parameter_by_slot({ axiom->get_parameters(), axiom->get_conjunctive_condition()->get_parameters() });
        ++program.num_rules;
        auto rule = Rule {};
        if (!builder.build(literal->get_atom()->get_predicate(),
                           literal->get_atom()->get_terms(),
                           { axiom->get_conjunctive_condition() },
                           parameter_by_slot,
                           "axiom:" + literal->get_atom()->get_predicate()->get_name(),
                           rule))
        {
            ++program.num_dropped_rules;
            continue;
        }
        rules.push_back(std::move(rule));
    }

    return rules;
}

/// @brief Drop rules whose fully ground static literals already decide the question, and rewrite the rest so
/// that only literals the fixpoint has to look at survive.
static bool simplify_static_literals(const Program& program, Rule& rule)
{
    auto body = std::vector<RuleAtom> {};
    for (auto& atom : rule.body)
    {
        if (program.relation_kind[atom.relation] != RelationKind::Static && program.relation_kind[atom.relation] != RelationKind::TypeDomain)
        {
            body.push_back(std::move(atom));
            continue;
        }
        const auto ground = std::all_of(atom.args.begin(), atom.args.end(), [](const RuleTerm& term) { return !term.is_variable; });
        if (!ground)
        {
            body.push_back(std::move(atom));
            continue;
        }
        auto values = std::vector<ObjectId> {};
        for (const auto& term : atom.args)
        {
            values.push_back(term.value);
        }
        if (!program.edb[atom.relation].contains(values.data()))
        {
            return false;
        }
    }
    rule.body = std::move(body);

    auto negative = std::vector<RuleAtom> {};
    for (auto& atom : rule.negative_static)
    {
        const auto ground = std::all_of(atom.args.begin(), atom.args.end(), [](const RuleTerm& term) { return !term.is_variable; });
        if (!ground)
        {
            negative.push_back(std::move(atom));
            continue;
        }
        auto values = std::vector<ObjectId> {};
        for (const auto& term : atom.args)
        {
            values.push_back(term.value);
        }
        if (program.edb[atom.relation].contains(values.data()))
        {
            return false;
        }
    }
    rule.negative_static = std::move(negative);

    /* mimir compiles the type hierarchy into unary static predicates and puts every one of them into the
       action's condition, so a rule over five parameters carries five `(object ?x)` literals on top of the
       five type literals that actually restrict anything. A unary static relation that holds every object
       filters nothing, so joining it is pure cost -- drop it, unless it is the only thing binding its
       variable, in which case it is what makes the rule safe. */
    {
        auto occurrences = std::vector<size_t>(rule.num_variables, 0);
        for (const auto& atom : rule.body)
        {
            for (const auto& term : atom.args)
            {
                if (term.is_variable)
                {
                    ++occurrences[term.value];
                }
            }
        }
        auto kept = std::vector<RuleAtom> {};
        for (auto& atom : rule.body)
        {
            const auto kind = program.relation_kind[atom.relation];
            const auto is_universal = (kind == RelationKind::Static || kind == RelationKind::TypeDomain)  //
                                      && atom.args.size() == 1 && atom.args[0].is_variable                //
                                      && program.edb[atom.relation].size() == program.object_by_id.size();
            if (is_universal && occurrences[atom.args[0].value] > 1)
            {
                --occurrences[atom.args[0].value];
                continue;
            }
            kept.push_back(std::move(atom));
        }
        rule.body = std::move(kept);
    }

    auto disequalities = std::vector<std::pair<RuleTerm, RuleTerm>> {};
    for (const auto& [lhs, rhs] : rule.disequalities)
    {
        if (!lhs.is_variable && !rhs.is_variable)
        {
            if (lhs.value == rhs.value)
            {
                return false;
            }
            continue;
        }
        disequalities.emplace_back(lhs, rhs);
    }
    rule.disequalities = std::move(disequalities);

    return true;
}

/// @brief (Re-)compile the join plan of every rule. Called twice: once from the typed-instance estimates and
/// once from the sizes the first fixpoint measured.
static void compile_plan(Program& program, const std::vector<Rule>& rules)
{
    /* Auxiliary relations and index slots of a previous compilation are thrown away. */
    const auto num_kept = program.relation_kind.size() - program.num_auxiliary_relations;
    program.relation_kind.resize(num_kept);
    program.relation_arity.resize(num_kept);
    program.relation_predicate.resize(num_kept);
    program.relation_name.resize(num_kept);
    program.relation_position_domain.resize(num_kept);
    program.relation_size_estimate.resize(num_kept);
    program.edb.resize(num_kept);
    program.num_auxiliary_relations = 0;
    program.num_edb_index_slots = 0;
    program.num_idb_index_slots = 0;
    program.steps.clear();
    program.shared_auxiliary_steps.clear();

    for (const auto& rule : rules)
    {
        compile_rule_with_best_seed(program, rule, program.steps);
    }

    /* The EDB indexes are built once and shared by every query, because a static relation never changes. */
    program.edb_indexes.assign(program.num_edb_index_slots, JoinIndex {});
    for (const auto& step : program.steps)
    {
        if (step.rhs_index != NO_INDEX && step.rhs_is_edb)
        {
            auto columns = step.join_rhs_columns;
            for (const auto& [column, value] : step.const_rhs_columns)
            {
                columns.push_back(column);
            }
            program.edb_indexes[step.rhs_index].set_columns(std::move(columns));
            program.edb_indexes[step.rhs_index].refresh(program.edb[step.rhs]);
        }
    }
}

}

namespace mimir::search
{

using namespace mimir::search::relaxed_reachability;

/**
 * ReachableTuples
 */

ReachableTuples::ReachableTuples(const relaxed_reachability::Relation* relation, const formalism::ObjectList* objects_by_id) :
    m_relation(relation),
    m_objects_by_id(objects_by_id)
{
}

size_t ReachableTuples::get_arity() const { return m_relation ? m_relation->get_arity() : 0; }

size_t ReachableTuples::size() const { return m_relation ? m_relation->size() : 0; }

bool ReachableTuples::empty() const { return size() == 0; }

formalism::ObjectList ReachableTuples::operator[](size_t position) const
{
    auto result = ObjectList {};
    write(position, result);
    return result;
}

void ReachableTuples::write(size_t position, formalism::ObjectList& out) const
{
    out.clear();
    if (!m_relation)
    {
        return;
    }
    const auto* tuple = m_relation->get_tuple(position);
    for (uint32_t i = 0; i < m_relation->get_arity(); ++i)
    {
        out.push_back((*m_objects_by_id)[tuple[i]]);
    }
}

/**
 * ReachabilityTable
 */

ReachabilityTable::ReachabilityTable(std::shared_ptr<const relaxed_reachability::Program> program,
                                     std::unique_ptr<relaxed_reachability::Database> database) :
    m_program(std::move(program)),
    m_database(std::move(database))
{
}

ReachabilityTable::~ReachabilityTable() = default;

ReachabilityTable::ReachabilityTable(ReachabilityTable&& other) noexcept = default;

ReachabilityTable& ReachabilityTable::operator=(ReachabilityTable&& other) noexcept = default;

template<IsStaticOrFluentOrDerivedTag P>
static bool table_is_reachable(const Program& program, const Database& database, Predicate<P> predicate, const ObjectList& objects)
{
    const auto relation_id = program.get_relation_id(predicate);
    if (relation_id == NO_RELATION || objects.size() != program.relation_arity[relation_id])
    {
        return false;
    }
    auto values = std::vector<ObjectId> {};
    values.reserve(objects.size());
    for (const auto object : objects)
    {
        const auto id = program.get_object_id(object);
        if (id == NO_OBJECT)
        {
            return false;  ///< an object of another problem can never occur in a tuple of this one
        }
        values.push_back(id);
    }
    return database.get_relation(relation_id).contains(values.data());
}

bool ReachabilityTable::is_reachable(formalism::Predicate<formalism::FluentTag> predicate, const formalism::ObjectList& objects) const
{
    return table_is_reachable(*m_program, *m_database, predicate, objects);
}

bool ReachabilityTable::is_reachable(formalism::Predicate<formalism::DerivedTag> predicate, const formalism::ObjectList& objects) const
{
    return table_is_reachable(*m_program, *m_database, predicate, objects);
}

bool ReachabilityTable::is_reachable(formalism::GroundAtom<formalism::FluentTag> atom) const
{
    return is_reachable(atom->get_predicate(), atom->get_objects());
}

bool ReachabilityTable::is_reachable(formalism::GroundAtom<formalism::DerivedTag> atom) const
{
    return is_reachable(atom->get_predicate(), atom->get_objects());
}

ReachableTuples ReachabilityTable::get_reachable_tuples(formalism::Predicate<formalism::FluentTag> predicate) const
{
    const auto relation_id = m_program->get_relation_id(predicate);
    return ReachableTuples(relation_id == NO_RELATION ? nullptr : &m_database->get_relation(relation_id), &m_program->object_by_id);
}

ReachableTuples ReachabilityTable::get_reachable_tuples(formalism::Predicate<formalism::DerivedTag> predicate) const
{
    const auto relation_id = m_program->get_relation_id(predicate);
    return ReachableTuples(relation_id == NO_RELATION ? nullptr : &m_database->get_relation(relation_id), &m_program->object_by_id);
}

size_t ReachabilityTable::get_num_reachable_fluent_atoms() const { return m_database->get_num_tuples(RelationKind::Fluent); }

size_t ReachabilityTable::get_num_reachable_derived_atoms() const { return m_database->get_num_tuples(RelationKind::Derived); }

size_t ReachabilityTable::get_num_reachable_atoms() const { return get_num_reachable_fluent_atoms() + get_num_reachable_derived_atoms(); }

bool ReachabilityTable::is_goal_reachable() const { return m_database->is_goal_reached(); }

size_t ReachabilityTable::get_num_fixpoint_rounds() const { return m_database->get_num_rounds(); }

/**
 * RelaxedReachability
 */

RelaxedReachability::RelaxedReachability(formalism::Problem problem, RelaxedReachabilityOptions options) :
    m_problem(std::move(problem)),
    m_options(options),
    m_program(),
    m_table(),
    m_statistics()
{
}

RelaxedReachability::~RelaxedReachability() = default;

std::shared_ptr<const RelaxedReachability> RelaxedReachability::create(const formalism::Problem& problem, const RelaxedReachabilityOptions& options)
{
    auto result = std::shared_ptr<RelaxedReachability>(new RelaxedReachability(problem, options));

    const auto compile_start = std::chrono::high_resolution_clock::now();

    auto program = std::make_shared<Program>();
    program->problem = problem;
    program->options = options;

    /* Objects get dense ids: the engine works on `uint32_t` tuples throughout and only maps back at the API. */
    for (const auto object : problem->get_problem_and_domain_objects())
    {
        program->id_by_object.emplace(object, ObjectId(program->object_by_id.size()));
        program->object_by_id.push_back(object);
    }

    auto compiler = Compiler(*program);

    /* Relation 0 is the truth value every rule's first join starts from. */
    program->true_relation = register_relation(*program, RelationKind::True, 0, nullptr, "$true");
    program->edb[program->true_relation].insert(nullptr);

    register_predicates(*program, compiler, problem->get_domain()->get_predicates<StaticTag>(), RelationKind::Static);
    register_predicates(*program, compiler, problem->get_domain()->get_predicates<FluentTag>(), RelationKind::Fluent);
    register_predicates(*program, compiler, problem->get_problem_and_domain_derived_predicates(), RelationKind::Derived);

    /* Static EDB. */
    {
        auto values = std::vector<ObjectId> {};
        for (const auto atom : problem->get_static_initial_atoms())
        {
            const auto relation_id = program->get_relation_id(atom->get_predicate());
            if (relation_id == NO_RELATION)
            {
                continue;  ///< the `=` predicate, which is evaluated instead of stored
            }
            values.clear();
            for (const auto object : atom->get_objects())
            {
                values.push_back(program->get_object_id(object));
            }
            program->edb[relation_id].insert(values.data());
        }
        for (RelationId id = 0; id < program->relation_kind.size(); ++id)
        {
            if (program->relation_kind[id] == RelationKind::Static)
            {
                program->relation_size_estimate[id] = double(program->edb[id].size());
                program->relation_position_domain[id] = compute_position_domains(program->edb[id]);
            }
        }
    }

    /* Initial fluent atoms, kept as tuples so a restricted query can leave some of them out. */
    for (const auto atom : problem->get_fluent_initial_atoms())
    {
        const auto relation_id = program->get_relation_id(atom->get_predicate());
        if (relation_id == NO_RELATION)
        {
            continue;
        }
        auto tuple = RelationTuple { relation_id, {} };
        for (const auto object : atom->get_objects())
        {
            tuple.values.push_back(program->get_object_id(object));
        }
        program->initial_fluent_atoms.push_back(std::move(tuple));
    }

    /* Goal. Negative goal atoms are ignored for the same reason negative preconditions are. */
    program->static_goal_holds = problem->static_goal_holds();
    for (const auto atom : problem->get_goal_atoms<PositiveTag, FluentTag>())
    {
        auto tuple = RelationTuple { program->get_relation_id(atom->get_predicate()), {} };
        for (const auto object : atom->get_objects())
        {
            tuple.values.push_back(program->get_object_id(object));
        }
        program->goal_tuples.push_back(std::move(tuple));
    }
    for (const auto atom : problem->get_goal_atoms<PositiveTag, DerivedTag>())
    {
        auto tuple = RelationTuple { program->get_relation_id(atom->get_predicate()), {} };
        for (const auto object : atom->get_objects())
        {
            tuple.values.push_back(program->get_object_id(object));
        }
        program->goal_tuples.push_back(std::move(tuple));
    }

    /* Rules. */
    auto rules = build_rules(*program, compiler);
    {
        auto kept = std::vector<Rule> {};
        for (auto& rule : rules)
        {
            if (simplify_static_literals(*program, rule))
            {
                kept.push_back(std::move(rule));
            }
            else
            {
                ++program->num_dropped_rules;
            }
        }
        rules = std::move(kept);
    }

    compile_plan(*program, rules);

    const auto compile_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - compile_start).count();

    /* The unrestricted fixpoint. */
    const auto fixpoint_start = std::chrono::high_resolution_clock::now();
    auto database = std::make_unique<Database>(*program);
    database->run(false);
    const auto fixpoint_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - fixpoint_start).count();

    auto& statistics = result->m_statistics;
    program->relation_capacity_hint.assign(program->relation_kind.size(), 0);
    for (RelationId id = 0; id < program->relation_kind.size(); ++id)
    {
        program->relation_capacity_hint[id] = database->get_relation(id).size();
        if (program->relation_kind[id] == RelationKind::Auxiliary)
        {
            statistics.num_auxiliary_tuples += database->get_relation(id).size();
        }
    }
    statistics.num_rules = program->num_rules;
    statistics.num_dropped_rules = program->num_dropped_rules;
    statistics.num_join_steps = program->steps.size();
    statistics.num_relations = program->relation_kind.size();
    statistics.num_auxiliary_relations = program->num_auxiliary_relations;
    statistics.num_fixpoint_rounds = database->get_num_rounds();
    statistics.num_reachable_fluent_atoms = database->get_num_tuples(RelationKind::Fluent);
    statistics.num_reachable_derived_atoms = database->get_num_tuples(RelationKind::Derived);
    statistics.num_static_tuples = database->get_num_tuples(RelationKind::Static) + database->get_num_tuples(RelationKind::TypeDomain);
    statistics.compile_time_ms = compile_ms;
    statistics.fixpoint_time_ms = fixpoint_ms;

    result->m_program = program;
    result->m_table = std::make_unique<ReachabilityTable>(program, std::move(database));

    return result;
}

bool RelaxedReachability::is_reachable(formalism::Predicate<formalism::FluentTag> predicate, const formalism::ObjectList& objects) const
{
    return m_table->is_reachable(predicate, objects);
}

bool RelaxedReachability::is_reachable(formalism::Predicate<formalism::DerivedTag> predicate, const formalism::ObjectList& objects) const
{
    return m_table->is_reachable(predicate, objects);
}

bool RelaxedReachability::is_reachable(formalism::GroundAtom<formalism::FluentTag> atom) const { return m_table->is_reachable(atom); }

bool RelaxedReachability::is_reachable(formalism::GroundAtom<formalism::DerivedTag> atom) const { return m_table->is_reachable(atom); }

ReachableTuples RelaxedReachability::get_reachable_tuples(formalism::Predicate<formalism::FluentTag> predicate) const
{
    return m_table->get_reachable_tuples(predicate);
}

ReachableTuples RelaxedReachability::get_reachable_tuples(formalism::Predicate<formalism::DerivedTag> predicate) const
{
    return m_table->get_reachable_tuples(predicate);
}

size_t RelaxedReachability::get_num_reachable_atoms() const { return m_table->get_num_reachable_atoms(); }

bool RelaxedReachability::is_goal_reachable() const { return m_table->is_goal_reachable(); }

const ReachabilityTable& RelaxedReachability::get_table() const { return *m_table; }

/// @brief Translate the caller's ground atoms into relation tuples, dropping any that name an object or a
/// predicate this problem does not have (such an atom can never be derived, so forbidding it is a no-op).
static std::vector<RelationTuple> to_relation_tuples(const Program& program, const RelaxedReachability::ForbiddenAtomList& forbidden)
{
    auto result = std::vector<RelationTuple> {};
    result.reserve(forbidden.size());
    for (const auto& [predicate, objects] : forbidden)
    {
        const auto relation_id = program.get_relation_id(predicate);
        if (relation_id == NO_RELATION || objects.size() != program.relation_arity[relation_id])
        {
            continue;
        }
        auto tuple = RelationTuple { relation_id, {} };
        auto valid = true;
        for (const auto object : objects)
        {
            const auto id = program.get_object_id(object);
            if (id == NO_OBJECT)
            {
                valid = false;
                break;
            }
            tuple.values.push_back(id);
        }
        if (valid)
        {
            result.push_back(std::move(tuple));
        }
    }
    return result;
}

ReachabilityTable RelaxedReachability::compute_restricted(const ForbiddenAtomList& forbidden) const
{
    auto database = std::make_unique<Database>(*m_program);
    database->set_forbidden(to_relation_tuples(*m_program, forbidden));
    database->run(false);
    return ReachabilityTable(m_program, std::move(database));
}

static RelaxedReachability::ForbiddenAtomList to_forbidden_list(const formalism::GroundAtomList<formalism::FluentTag>& atoms)
{
    auto result = RelaxedReachability::ForbiddenAtomList {};
    result.reserve(atoms.size());
    for (const auto atom : atoms)
    {
        result.emplace_back(atom->get_predicate(), atom->get_objects());
    }
    return result;
}

ReachabilityTable RelaxedReachability::compute_restricted(const formalism::GroundAtomList<formalism::FluentTag>& forbidden) const
{
    return compute_restricted(to_forbidden_list(forbidden));
}

bool RelaxedReachability::is_goal_reachable_without(const ForbiddenAtomList& forbidden) const
{
    auto database = Database(*m_program);
    database.set_forbidden(to_relation_tuples(*m_program, forbidden));
    database.run(true);
    return database.is_goal_reached();
}

bool RelaxedReachability::is_goal_reachable_without(const formalism::GroundAtomList<formalism::FluentTag>& forbidden) const
{
    return is_goal_reachable_without(to_forbidden_list(forbidden));
}

const formalism::Problem& RelaxedReachability::get_problem() const { return m_problem; }

const RelaxedReachabilityOptions& RelaxedReachability::get_options() const { return m_options; }

const RelaxedReachabilityStatistics& RelaxedReachability::get_statistics() const { return m_statistics; }

}
