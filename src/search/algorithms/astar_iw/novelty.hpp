#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_NOVELTY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_NOVELTY_HPP_

#include "mimir/search/algorithms/astar_iw.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <concepts>
#include <memory>
#include <ranges>
#include <type_traits>
#include <variant>
#include <vector>

namespace mimir::search::iw::astar_iw_friend
{

/// Adapter that deliberately reuses Abstracted IW's private feature interning and
/// tuple enumeration. Its Boolean novelty table remains untouched; only the
/// minimum-g maps below are updated.
class AbstractedMinimumGNoveltyTable
{
private:
    using FeatureId = AbstractedNoveltyPruningStrategyImpl::FeatureId;
    using PairKey = AbstractedNoveltyPruningStrategyImpl::PairKey;
    using PairKeyHash = AbstractedNoveltyPruningStrategyImpl::PairKeyHash;
    using TripleKey = AbstractedNoveltyPruningStrategyImpl::TripleKey;
    using TripleKeyHash = AbstractedNoveltyPruningStrategyImpl::TripleKeyHash;
    using AtomFeatureGroup = AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup;
    using GeneratedTuples = AbstractedNoveltyPruningStrategyImpl::GeneratedTuples;

    AbstractedNoveltyPruningStrategyImpl m_feature_generator;
    absl::flat_hash_map<FeatureId, ContinuousCost> m_singletons;
    absl::flat_hash_map<PairKey, ContinuousCost, PairKeyHash> m_pairs;
    absl::flat_hash_map<TripleKey, ContinuousCost, TripleKeyHash> m_triples;

    GeneratedTuples generate_state_tuples(const State& state) const
    {
        auto groups = m_feature_generator.state_groups(state);
        for (auto& group : groups)
        {
            group.m_added = true;
        }
        return m_feature_generator.generate_tuples(groups, false);
    }

    GeneratedTuples generate_transition_tuples(const State& state, const State& succ_state) const
    {
        return m_feature_generator.generate_tuples(m_feature_generator.successor_groups(state, m_feature_generator.atom_indices_key(succ_state)), false);
    }

    bool update(const GeneratedTuples& tuples, ContinuousCost g_value)
    {
        auto improved = false;
        const auto lower = [&](auto& table, const auto& key)
        {
            const auto [it, inserted] = table.try_emplace(key, g_value);
            if (inserted)
            {
                improved = true;
            }
            else if (g_value < it->second)
            {
                it->second = g_value;
                improved = true;
            }
        };

        for (const auto key : tuples.m_singles)
        {
            lower(m_singletons, key);
        }
        for (const auto& key : tuples.m_pairs)
        {
            lower(m_pairs, key);
        }
        for (const auto& key : tuples.m_triples)
        {
            lower(m_triples, key);
        }
        return improved;
    }

    bool contains_at_g(const GeneratedTuples& tuples, ContinuousCost g_value) const
    {
        const auto has = [&](const auto& table, const auto& key)
        {
            const auto it = table.find(key);
            return it != table.end() && it->second == g_value;
        };
        return std::ranges::any_of(tuples.m_singles, [&](const auto key) { return has(m_singletons, key); })
               || std::ranges::any_of(tuples.m_pairs, [&](const auto& key) { return has(m_pairs, key); })
               || std::ranges::any_of(tuples.m_triples, [&](const auto& key) { return has(m_triples, key); });
    }

public:
    AbstractedMinimumGNoveltyTable(formalism::Problem problem, size_t width, bool base_abstracted, bool preserve_goal_atoms) :
        m_feature_generator(std::move(problem), width, base_abstracted, preserve_goal_atoms, false)
    {
    }

    bool initialize(const State& state, ContinuousCost g_value) { return update(generate_state_tuples(state), g_value); }

    bool test_and_update(const State& state, const State& succ_state, ContinuousCost g_value)
    {
        return update(generate_transition_tuples(state, succ_state), g_value);
    }

    bool test_at_g(const State& state, ContinuousCost g_value) const { return contains_at_g(generate_state_tuples(state), g_value); }
};

}

namespace mimir::search::astar_iw
{

class MinimumGNoveltyBackend
{
private:
    using ClassicalTable = std::vector<std::unique_ptr<iw::MinimumGNoveltyTable>>;
    using AbstractedTable = std::unique_ptr<iw::astar_iw_friend::AbstractedMinimumGNoveltyTable>;
    using Table = std::variant<ClassicalTable, AbstractedTable>;
    Table m_table;

public:
    MinimumGNoveltyBackend(const formalism::Problem& problem, NoveltyFeatureMode mode, size_t width, bool preserve_goal_atoms) :
        m_table(
            [&]() -> Table
            {
                if (mode == NoveltyFeatureMode::CLASSICAL)
                {
                    auto tables = ClassicalTable {};
                    tables.reserve(width);
                    for (size_t arity = 1; arity <= width; ++arity)
                    {
                        tables.push_back(std::make_unique<iw::MinimumGNoveltyTable>(arity));
                    }
                    return tables;
                }
                return std::make_unique<iw::astar_iw_friend::AbstractedMinimumGNoveltyTable>(problem,
                                                                                             width,
                                                                                             mode == NoveltyFeatureMode::BASE_ABSTRACTED,
                                                                                             preserve_goal_atoms);
            }())
    {
    }

    bool initialize(const State& state, ContinuousCost g_value)
    {
        return std::visit(
            [&](auto& table)
            {
                if constexpr (std::same_as<std::decay_t<decltype(table)>, ClassicalTable>)
                {
                    auto improved = false;
                    for (auto& arity_table : table)
                    {
                        improved = arity_table->test_novelty_and_update_table(state, g_value) || improved;
                    }
                    return improved;
                }
                else
                {
                    return table->initialize(state, g_value);
                }
            },
            m_table);
    }

    bool test_and_update(const State& state, const State& succ_state, ContinuousCost g_value)
    {
        return std::visit(
            [&](auto& table)
            {
                if constexpr (std::same_as<std::decay_t<decltype(table)>, ClassicalTable>)
                {
                    auto improved = false;
                    for (auto& arity_table : table)
                    {
                        improved = arity_table->test_novelty_and_update_table(state, succ_state, g_value) || improved;
                    }
                    return improved;
                }
                else
                {
                    return table->test_and_update(state, succ_state, g_value);
                }
            },
            m_table);
    }

    bool test_at_g(const State& state, ContinuousCost g_value) const
    {
        return std::visit(
            [&](const auto& table)
            {
                if constexpr (std::same_as<std::decay_t<decltype(table)>, ClassicalTable>)
                {
                    return std::ranges::any_of(table, [&](const auto& arity_table) { return arity_table->test_novelty_at_g_read_only(state, g_value); });
                }
                else
                {
                    return table->test_at_g(state, g_value);
                }
            },
            m_table);
    }
};

}

#endif
