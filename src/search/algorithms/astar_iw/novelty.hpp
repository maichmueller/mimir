#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_NOVELTY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_NOVELTY_HPP_

#include "mimir/search/algorithms/astar_iw.hpp"
#include "mimir/search/algorithms/iw/landmark_novelty_table.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"

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
///
/// Every key carries a landmark coordinate, so this one class covers both abstracted IW and its
/// landmark-restricted variant. Without a landmark set every state sits on the single `BOT`
/// coordinate and the keys are constant in that field, reproducing plain abstracted behavior.
///
/// The landmark coordinate stays a *concrete* landmark rank while only the free coordinates are
/// abstracted. Abstracting the landmark too would erase exactly the identity that makes the
/// coordinate discriminating, collapsing landmarks of the same predicate and type signature onto
/// one another.
class AbstractedMinimumGNoveltyTable
{
private:
    using FeatureId = AbstractedNoveltyPruningStrategyImpl::FeatureId;
    using AtomFeatureGroup = AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup;
    using GeneratedTuples = AbstractedNoveltyPruningStrategyImpl::GeneratedTuples;

    /* The strategy's own `PairKey`/`TripleKey` are left alone: they key its Boolean tables, which
       must keep their layout. These are the landmark-extended counterparts, used only here. */

    struct RankedSingle
    {
        uint32_t m_rank;
        FeatureId m_a;

        bool operator==(const RankedSingle& other) const noexcept = default;
    };

    struct RankedPair
    {
        uint32_t m_rank;
        FeatureId m_a;
        FeatureId m_b;

        bool operator==(const RankedPair& other) const noexcept = default;
    };

    struct RankedTriple
    {
        uint32_t m_rank;
        FeatureId m_a;
        FeatureId m_b;
        FeatureId m_c;

        bool operator==(const RankedTriple& other) const noexcept = default;
    };

    struct RankedKeyHash
    {
        size_t operator()(const RankedSingle& key) const noexcept
        {
            size_t seed = 0;
            loki::hash_combine(seed, key.m_rank);
            loki::hash_combine(seed, key.m_a);
            return seed;
        }
        size_t operator()(const RankedPair& key) const noexcept
        {
            size_t seed = 0;
            loki::hash_combine(seed, key.m_rank);
            loki::hash_combine(seed, key.m_a);
            loki::hash_combine(seed, key.m_b);
            return seed;
        }
        size_t operator()(const RankedTriple& key) const noexcept
        {
            size_t seed = 0;
            loki::hash_combine(seed, key.m_rank);
            loki::hash_combine(seed, key.m_a);
            loki::hash_combine(seed, key.m_b);
            loki::hash_combine(seed, key.m_c);
            return seed;
        }
    };

    AbstractedNoveltyPruningStrategyImpl m_feature_generator;
    LandmarkCoordinates m_coordinates;
    absl::flat_hash_map<RankedSingle, ContinuousCost, RankedKeyHash> m_singletons;
    absl::flat_hash_map<RankedPair, ContinuousCost, RankedKeyHash> m_pairs;
    absl::flat_hash_map<RankedTriple, ContinuousCost, RankedKeyHash> m_triples;
    bool m_lowered_existing_label = false;

    mutable std::vector<uint32_t> m_scratch_flipped_ranks;
    mutable std::vector<uint32_t> m_scratch_kept_ranks;

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

    bool update(const GeneratedTuples& tuples, const std::vector<uint32_t>& ranks, ContinuousCost g_value)
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
                m_lowered_existing_label = true;
            }
        };

        for (const auto rank : ranks)
        {
            for (const auto key : tuples.m_singles)
            {
                lower(m_singletons, RankedSingle { rank, key });
            }
            for (const auto& key : tuples.m_pairs)
            {
                lower(m_pairs, RankedPair { rank, key.m_a, key.m_b });
            }
            for (const auto& key : tuples.m_triples)
            {
                lower(m_triples, RankedTriple { rank, key.m_a, key.m_b, key.m_c });
            }
        }
        return improved;
    }

    bool contains_at_g(const GeneratedTuples& tuples, const std::vector<uint32_t>& ranks, ContinuousCost g_value) const
    {
        const auto has = [&](const auto& table, const auto& key)
        {
            const auto it = table.find(key);
            return it != table.end() && it->second == g_value;
        };
        return std::ranges::any_of(
            ranks,
            [&](const auto rank)
            {
                return std::ranges::any_of(tuples.m_singles, [&](const auto key) { return has(m_singletons, RankedSingle { rank, key }); })
                       || std::ranges::any_of(tuples.m_pairs, [&](const auto& key) { return has(m_pairs, RankedPair { rank, key.m_a, key.m_b }); })
                       || std::ranges::any_of(tuples.m_triples,
                                              [&](const auto& key) { return has(m_triples, RankedTriple { rank, key.m_a, key.m_b, key.m_c }); });
            });
    }

public:
    AbstractedMinimumGNoveltyTable(formalism::Problem problem,
                                   size_t width,
                                   bool base_abstracted,
                                   bool preserve_goal_atoms,
                                   AtomIndexList landmark_atom_indices = {}) :
        m_feature_generator(std::move(problem), width, base_abstracted, preserve_goal_atoms, false),
        m_coordinates(std::move(landmark_atom_indices))
    {
    }

    bool initialize(const State& state, ContinuousCost g_value)
    {
        m_coordinates.collect(state, m_scratch_kept_ranks);
        return update(generate_state_tuples(state), m_scratch_kept_ranks, g_value);
    }

    /// @brief Lower the labels of the transition's features, splitting on the landmark coordinate
    /// exactly as `LandmarkNoveltyTable` does: a coordinate that just flipped on pairs with *all*
    /// features of the successor, an already-true one only with the transition's features.
    bool test_and_update(const State& state, const State& succ_state, ContinuousCost g_value)
    {
        m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

        auto improved = false;
        if (!m_scratch_flipped_ranks.empty())
        {
            improved = update(generate_state_tuples(succ_state), m_scratch_flipped_ranks, g_value) || improved;
        }
        if (!m_scratch_kept_ranks.empty())
        {
            improved = update(generate_transition_tuples(state, succ_state), m_scratch_kept_ranks, g_value) || improved;
        }
        return improved;
    }

    bool test_at_g(const State& state, ContinuousCost g_value) const
    {
        m_coordinates.collect(state, m_scratch_kept_ranks);
        return contains_at_g(generate_state_tuples(state), m_scratch_kept_ranks, g_value);
    }

    bool has_lowered_existing_label() const { return m_lowered_existing_label; }
};

}

namespace mimir::search::astar_iw
{

class MinimumGNoveltyBackend
{
private:
    /// One table of arity `width`, not one per arity: `StateTupleIndexGenerator` appends a
    /// placeholder atom to the state, so an arity-k table already indexes every tuple of
    /// size below k as well (this is why IW's arity-k pass also uses a single table). The
    /// per-arity tables were exact duplicates of information the widest table already held,
    /// and enumerating them doubled the tuple work on every generated state at width 2.
    using ClassicalTable = std::unique_ptr<iw::MinimumGNoveltyTable>;
    /// Landmark-restricted classical features; shares `ClassicalTable`'s call shape by design.
    using LandmarkTable = std::unique_ptr<iw::LandmarkMinimumGNoveltyTable>;
    using AbstractedTable = std::unique_ptr<iw::astar_iw_friend::AbstractedMinimumGNoveltyTable>;
    using Table = std::variant<ClassicalTable, LandmarkTable, AbstractedTable>;
    Table m_table;

public:
    MinimumGNoveltyBackend(const formalism::Problem& problem,
                           NoveltyFeatureMode mode,
                           size_t width,
                           bool preserve_goal_atoms,
                           const landmarks::FactLandmarkGraph& landmark_novelty_graph) :
        m_table(
            [&]() -> Table
            {
                auto landmark_atom_indices = iw::AtomIndexList {};
                if (landmark_novelty_graph)
                {
                    const auto& indices = landmark_novelty_graph->get_landmark_atom_indices();
                    landmark_atom_indices.assign(indices.begin(), indices.end());
                }

                if (mode == NoveltyFeatureMode::CLASSICAL)
                {
                    if (landmark_novelty_graph)
                    {
                        return std::make_unique<iw::LandmarkMinimumGNoveltyTable>(std::move(landmark_atom_indices), width);
                    }
                    return std::make_unique<iw::MinimumGNoveltyTable>(width);
                }
                return std::make_unique<iw::astar_iw_friend::AbstractedMinimumGNoveltyTable>(problem,
                                                                                             width,
                                                                                             mode == NoveltyFeatureMode::BASE_ABSTRACTED,
                                                                                             preserve_goal_atoms,
                                                                                             std::move(landmark_atom_indices));
            }())
    {
    }

    bool initialize(const State& state, ContinuousCost g_value)
    {
        return std::visit(
            [&](auto& table)
            {
                /* Dispatch on the abstracted table, not the classical one: the landmark-restricted
                   table deliberately mirrors the classical call shape, so both share this arm. */
                if constexpr (std::same_as<std::decay_t<decltype(table)>, AbstractedTable>)
                {
                    return table->initialize(state, g_value);
                }
                else
                {
                    return table->test_novelty_and_update_table(state, g_value);
                }
            },
            m_table);
    }

    bool test_and_update(const State& state, const State& succ_state, ContinuousCost g_value)
    {
        return std::visit(
            [&](auto& table)
            {
                if constexpr (std::same_as<std::decay_t<decltype(table)>, AbstractedTable>)
                {
                    return table->test_and_update(state, succ_state, g_value);
                }
                else
                {
                    return table->test_novelty_and_update_table(state, succ_state, g_value);
                }
            },
            m_table);
    }

    bool test_at_g(const State& state, ContinuousCost g_value) const
    {
        return std::visit(
            [&](const auto& table)
            {
                if constexpr (std::same_as<std::decay_t<decltype(table)>, AbstractedTable>)
                {
                    return table->test_at_g(state, g_value);
                }
                else
                {
                    return table->test_novelty_at_g_read_only(state, g_value);
                }
            },
            m_table);
    }

    /// @brief Whether any tuple label was ever lowered after it was first set.
    ///
    /// A state is only pushed after `test_and_update` lowered some tuple to that state's g
    /// value. Until an already-finite label is lowered again, no state can have had such a
    /// tuple taken from it, so `test_at_g` is guaranteed to hold for every popped state and
    /// the whole check -- a full tuple enumeration per expansion -- can be skipped.
    bool may_have_stale_novelty() const
    {
        return std::visit([](const auto& table) { return table->has_lowered_existing_label(); }, m_table);
    }
};

}

#endif
