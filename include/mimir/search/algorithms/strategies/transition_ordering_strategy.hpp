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

#ifndef MIMIR_SEARCH_ALGORITHMS_STRATEGIES_TRANSITION_ORDERING_STRATEGY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_STRATEGIES_TRANSITION_ORDERING_STRATEGY_HPP_

#include "mimir/common/declarations.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/state.hpp"

#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace mimir::search
{

/// @brief `TransitionOrderingStrategyBase` is a CRTP base class for strategies that score and reorder
/// the candidate transitions generated at one BrFS/IW search-layer depth *before* novelty pruning
/// decides which of them are admitted. `score()` forwards to `Derived::score_impl(...)` and `prefer()`
/// forwards to `Derived::prefer_impl(...)`.
///
/// `prefer()` is a proper CRTP-forwarded member -- symmetric with `score()` -- rather than an ad-hoc
/// method only some strategies happen to define. This matters because the deferred BrFS admission loop
/// (see `src/search/algorithms/brfs/transition_ordered_layer_impl.hpp`) must stay generic over
/// `Ordering`: it sorts candidates via `ordering.prefer(a, b)` without ever naming a concrete strategy
/// type, so `prefer` has to be reachable through the same CRTP contract as `score`.
/// @tparam Derived is the derived strategy type.
template<class Derived>
class TransitionOrderingStrategyBase
{
private:
    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

    friend Derived;

public:
    static constexpr bool requires_deferred_novelty = Derived::requires_deferred_novelty;

    template<typename... Args>
    auto score(Args&&... args) const
    {
        return self().score_impl(std::forward<Args>(args)...);
    }

    template<typename ScoreT>
    bool prefer(const ScoreT& a, const ScoreT& b) const
    {
        return self().prefer_impl(a, b);
    }
};

/// @brief `TransitionOrderingStrategy` is satisfied by any type exposing a `requires_deferred_novelty`
/// bool. It deliberately does NOT require `score_impl`/`prefer_impl`: those are only ever called from
/// code paths gated behind `if constexpr (Ordering::requires_deferred_novelty)`, which is never
/// instantiated for a strategy with `requires_deferred_novelty == false`. This keeps
/// `QueuedTransitionOrderingStrategy` a valid model of this concept without defining either method.
template<typename T>
concept TransitionOrderingStrategy = requires {
    { T::requires_deferred_novelty } -> std::convertible_to<bool>;
};

/// @brief `QueuedTransitionOrderingStrategy` reproduces today's default search behavior: candidate
/// transitions are admitted in raw generation (queued) order. There is no scoring/reordering and no
/// deferred novelty admission pass, so this strategy defines neither `score_impl` nor `prefer_impl`.
class QueuedTransitionOrderingStrategy : public TransitionOrderingStrategyBase<QueuedTransitionOrderingStrategy>
{
public:
    static constexpr bool requires_deferred_novelty = false;
};

/// @brief Options controlling `LandmarkTransitionOrderingStrategy::prefer_impl`'s lexicographic
/// preference between two candidate transitions' `LandmarkTransitionScore`s. Each flag gates exactly
/// one comparison key; disabling a flag skips that key (falling through to the next) rather than
/// inverting it.
struct LandmarkTransitionOrderingOptions
{
    bool prefer_new_landmarks = true;
    bool prefer_unique_achievers = true;
    bool prefer_landmark_actions_when_deleting = true;
    bool prefer_fewer_deleted_landmarks = true;
};

/// @brief Per-transition landmark score computed by `LandmarkTransitionOrderingStrategy::score_impl`.
/// Fields are deliberately plain and human-inspectable rather than sign-folded into a single ranked
/// value, so tests (and callers) can inspect e.g. deletion counts directly.
struct LandmarkTransitionScore
{
    uint32_t num_new_landmarks;
    uint32_t num_new_unique_landmarks;
    bool unique_achiever_action;
    bool deletes_achieved_landmark;
    uint32_t num_deleted_achieved_landmarks;
};

/// @brief `LandmarkTransitionOrderingStrategy` scores candidate transitions by how they interact with
/// an approximate fact-landmark graph (`landmarks::FactLandmarkGraph`): transitions that newly achieve
/// landmarks -- especially via their unique achiever action -- are preferred over transitions that
/// delete already-achieved landmarks. Used to reorder all candidate transitions generated at one BrFS/
/// IW search-layer depth before novelty pruning decides admission (see `requires_deferred_novelty`).
class LandmarkTransitionOrderingStrategy : public TransitionOrderingStrategyBase<LandmarkTransitionOrderingStrategy>
{
private:
    landmarks::FactLandmarkGraph m_landmarks;
    LandmarkTransitionOrderingOptions m_options;

public:
    /// @brief Requires a graph with an achiever index: `score_impl` ranks a transition by whether
    /// its action is the *unique achiever* of a landmark, which a graph built without grounding
    /// cannot answer. Rejecting it here rather than at the first score call keeps the failure at
    /// configuration time, where the caller can still pick a different ordering.
    LandmarkTransitionOrderingStrategy(landmarks::FactLandmarkGraph landmarks,
                                       LandmarkTransitionOrderingOptions options = LandmarkTransitionOrderingOptions()) :
        m_landmarks(std::move(landmarks)),
        m_options(options)
    {
        if (m_landmarks && !m_landmarks->has_achiever_index())
        {
            throw std::logic_error("landmark graph carries no achiever index (built without grounding)");
        }
    }

    static constexpr bool requires_deferred_novelty = true;

    const landmarks::FactLandmarkGraph& get_landmarks() const { return m_landmarks; }
    const LandmarkTransitionOrderingOptions& get_options() const { return m_options; }

    /// @brief Score how the transition (`parent` --`action`--> `successor`) interacts with the
    /// landmark graph. `successor_depth` is accepted for interface uniformity with other transition
    /// ordering strategies but is unused by this one.
    LandmarkTransitionScore
    score_impl(const State& parent, formalism::GroundAction action, const State& successor, [[maybe_unused]] DiscreteCost successor_depth) const
    {
        const auto& parent_atoms = parent.get_atoms<formalism::FluentTag>();
        const auto& successor_atoms = successor.get_atoms<formalism::FluentTag>();

        auto num_new_landmarks = uint32_t(0);
        auto num_new_unique_landmarks = uint32_t(0);
        auto num_deleted_achieved_landmarks = uint32_t(0);

        // "new" = (successor.atoms<Fluent>() \ parent.atoms<Fluent>()) intersected with landmark atoms.
        // "deleted" = (parent.atoms<Fluent>() \ successor.atoms<Fluent>()) intersected with landmark atoms.
        // Landmark sets are typically small, so a single pass over the landmark atom indices (rather
        // than a dedicated FlatBitset mask accessor) is simplest and fine performance-wise.
        for (const auto atom_index : m_landmarks->get_landmark_atom_indices())
        {
            const auto in_parent = parent_atoms.get(atom_index);
            const auto in_successor = successor_atoms.get(atom_index);

            if (in_successor && !in_parent)
            {
                ++num_new_landmarks;

                const auto unique_achiever_index = m_landmarks->get_unique_achiever_action_index(atom_index);
                if (unique_achiever_index.has_value() && (*unique_achiever_index == action->get_index()))
                {
                    ++num_new_unique_landmarks;
                }
            }
            else if (in_parent && !in_successor)
            {
                ++num_deleted_achieved_landmarks;
            }
        }

        return LandmarkTransitionScore {
            num_new_landmarks,
            num_new_unique_landmarks,
            m_landmarks->is_unique_landmark_achiever(action),  // action-level property, independent of this specific transition
            (num_deleted_achieved_landmarks > 0),
            num_deleted_achieved_landmarks,
        };
    }

    /// @brief 5-key descending lexicographic preference between two candidate transitions' scores.
    /// Returns true if `a` should sort before `b`. Keys 1-3 prefer LARGER values; key 4 prefers
    /// SMALLER `num_deleted_achieved_landmarks` (fewer deletions wins) -- the opposite direction from
    /// keys 1-3. The 5th key (generation order) is free via `std::stable_sort`'s stability guarantee,
    /// so no explicit field is needed for it here.
    bool prefer_impl(const LandmarkTransitionScore& a, const LandmarkTransitionScore& b) const
    {
        if (m_options.prefer_new_landmarks && (a.num_new_landmarks != b.num_new_landmarks))
        {
            return a.num_new_landmarks > b.num_new_landmarks;
        }
        if (m_options.prefer_unique_achievers && (a.num_new_unique_landmarks != b.num_new_unique_landmarks))
        {
            return a.num_new_unique_landmarks > b.num_new_unique_landmarks;
        }
        if (m_options.prefer_landmark_actions_when_deleting && (a.unique_achiever_action != b.unique_achiever_action))
        {
            return a.unique_achiever_action;
        }
        if (m_options.prefer_fewer_deleted_landmarks && (a.num_deleted_achieved_landmarks != b.num_deleted_achieved_landmarks))
        {
            // NB: fewer deletions wins -- opposite direction from keys 1-3 above.
            return a.num_deleted_achieved_landmarks < b.num_deleted_achieved_landmarks;
        }
        return false;
    }
};

}

#endif
