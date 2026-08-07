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

#ifndef MIMIR_SEARCH_ALGORITHMS_SEARCH_CONTROL_HPP_
#define MIMIR_SEARCH_ALGORITHMS_SEARCH_CONTROL_HPP_

#include <atomic>
#include <cstdint>
#include <limits>

namespace mimir::search
{

/// @brief The bit of shared state that several searches running side by side need: a stop flag, the
/// best plan length anyone has found so far, and how far the certifier has completely processed.
///
/// Deliberately tiny and lock-free. Every field is `memory_order_relaxed`: none of them orders
/// access to anything else, they are read on hot paths (once per node expansion, once per rollout
/// step), and a reader that is one update stale merely does a little more work. The one piece of
/// data that genuinely needs ordering -- the incumbent plan itself -- lives behind a mutex in the
/// portfolio, and `incumbent_length` here is only the summary used to decide whether to look.
///
/// A null `SearchControl*` means "no coordination", and every consumer must be zero-overhead in
/// that case: one predictable null check per loop iteration.
struct SearchControl
{
    static constexpr uint32_t NO_DEPTH = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t NO_INCUMBENT = std::numeric_limits<uint32_t>::max();

    /// @brief Set by whoever decides the search is over; every participant stops at its next check.
    std::atomic<bool> cancel { false };

    /// @brief Length of the shortest plan any participant has published, or `NO_INCUMBENT`.
    /// Monotonically non-increasing.
    std::atomic<uint32_t> incumbent_length { NO_INCUMBENT };

    /// @brief The deepest g-layer the certifier has finished expanding, or `NO_DEPTH` if it has not
    /// finished one yet. Monotonically non-decreasing. Since the certifier tests the goal when a
    /// node is *popped*, finishing layer d proves no plan of length <= d exists in the searched
    /// space, so `completed_depth + 1` is a lower bound on the optimal plan length.
    std::atomic<uint32_t> completed_depth { NO_DEPTH };

    /// @brief Total node expansions across all participants, for a shared expansion budget.
    std::atomic<uint64_t> total_expansions { 0 };

    /// @brief Cap on `total_expansions`. Reaching it cancels the run.
    ///
    /// The cap lives here rather than only in the coordinator because a coordinator can only check
    /// it between participants (serial) or between polls (parallel), and a participant that is deep
    /// inside one long search would overshoot by its whole remaining run. Enforcing it in
    /// `add_expansions` makes every participant stop itself at its own next expansion.
    std::atomic<uint64_t> max_total_expansions { std::numeric_limits<uint64_t>::max() };

    /// @brief Set when the search publishing `completed_depth` finished its entire space without
    /// finding a plan, which retracts the lower bound.
    ///
    /// The bound means "no plan of length <= completed_depth exists *in the space I searched*". That
    /// certifies somebody else's plan only while it is still plausible that their plan is in that
    /// space too. A search that exhausted its space and found nothing, next to a worker that did
    /// find something, is proof that it is not -- so from that point the bound certifies nothing.
    std::atomic<bool> lower_bound_invalidated { false };

    SearchControl() = default;

    SearchControl(const SearchControl&) = delete;
    SearchControl& operator=(const SearchControl&) = delete;

    bool is_canceled() const { return cancel.load(std::memory_order_relaxed); }

    void request_cancel() { cancel.store(true, std::memory_order_relaxed); }

    uint32_t get_incumbent_length() const { return incumbent_length.load(std::memory_order_relaxed); }

    /// @brief Publish `length` if it beats the current incumbent.
    /// @return true if this call installed the new value, i.e. the caller now owns publishing the
    /// corresponding plan. Losing the race means somebody else found something at least as short.
    bool improve_incumbent_length(uint32_t length)
    {
        auto current = incumbent_length.load(std::memory_order_relaxed);
        while (length < current)
        {
            if (incumbent_length.compare_exchange_weak(current, length, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }

    uint32_t get_completed_depth() const { return completed_depth.load(std::memory_order_relaxed); }

    /// @brief Publish that layer `depth` is fully expanded, if that is news.
    void publish_completed_depth(uint32_t depth)
    {
        auto current = completed_depth.load(std::memory_order_relaxed);
        while (current == NO_DEPTH || depth > current)
        {
            if (completed_depth.compare_exchange_weak(current, depth, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    void invalidate_lower_bound() { lower_bound_invalidated.store(true, std::memory_order_relaxed); }

    bool is_lower_bound_invalidated() const { return lower_bound_invalidated.load(std::memory_order_relaxed); }

    /// @brief The monotone lower bound on the optimal plan length implied by `completed_depth`:
    /// 0 while nothing has been completed, `completed_depth + 1` afterwards.
    uint32_t get_lower_bound() const
    {
        const auto depth = get_completed_depth();
        return (depth == NO_DEPTH) ? 0u : (depth + 1u);
    }

    /// @brief Whether an incumbent of length `length` is already proven shortest by the certifier's
    /// progress: every node at depth `length - 1` has been popped and was not a goal, and the bound
    /// has not been retracted.
    bool is_incumbent_certified(uint32_t length) const
    {
        return (length != NO_INCUMBENT) && !is_lower_bound_invalidated() && (get_lower_bound() >= length);
    }

    void add_expansions(uint64_t count)
    {
        const auto total = total_expansions.fetch_add(count, std::memory_order_relaxed) + count;
        if (total >= max_total_expansions.load(std::memory_order_relaxed))
        {
            request_cancel();
        }
    }

    uint64_t get_total_expansions() const { return total_expansions.load(std::memory_order_relaxed); }
};

}

#endif
