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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_PARALLEL_ROLLOUTS_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_PARALLEL_ROLLOUTS_HPP_

#include "mimir/common/types_cista.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/declarations.hpp"

#include <cstdint>
#include <vector>

namespace mimir::search::iw
{

/// @brief Options for a batch of independent, differently-seeded IW rollouts that all
/// start from the same state and share one already-parsed, already-grounded `Problem`.
///
/// The batch runs with real thread-level parallelism. This is sound because a grounded
/// search mutates nothing at the `Problem` level except the `valla` interning tables, and
/// each rollout is given its own (see `StateRepositoryImpl::PrivateInterningTables`).
/// See docs/PARALLEL_IW_ROLLOUTS.md for the measurements behind this design.
///
/// The conditions that make it sound, all enforced or maintained internally:
///
///   1. The context must be grounded. Lifted grows the `Problem`'s `Repositories` during
///      search through `ProblemImpl::ground(...)`, and `loki::IndexedHashSet` has no
///      synchronization. Rejected at the top of `find_rollouts_parallel`.
///   2. `beam_width` must be unset -- the randomized layer ordering cannot score eagerly.
///      Also rejected there.
///   3. **No caller-owned `State` may reach a worker thread.** `State` holds a
///      `SharedObjectPoolPtr<UnpackedStateImpl>` with a NON-ATOMIC refcount pointing into
///      the caller's unsynchronized `SharedObjectPool`, so copying one into N tasks races
///      that refcount; a lost increment frees a live object back to the caller's pool and
///      it is reissued to a different state. `find_rollouts_parallel` therefore clears
///      `options.start_state` before anything is copied per worker, and each rollout
///      re-creates its start state in its own repository from a dense description taken on
///      the calling thread. Callers may pass any `State` they like -- it never crosses.
///
/// Points 1 and 2 reject; point 3 is handled for the caller. Preserve all three when
/// changing this file: violating (3) does not crash, it silently corrupts the CALLER's
/// states, which then enumerate actions whose own preconditions do not hold.
struct ParallelRolloutOptions
{
    /// @brief One rollout is run per seed. The seed drives both the randomized layer
    /// ordering and the equal-score tie-breaking, so distinct seeds give distinct rollouts.
    std::vector<uint64_t> seeds = {};

    /// @brief Worker threads to use. 0 means `std::thread::hardware_concurrency()`, capped
    /// at the number of seeds.
    uint32_t num_threads = 0;

    /// @brief Template applied to every rollout. `layer_ordering_strategy`,
    /// `equal_score_tie_seed` and `randomize_equal_score_ties` are overridden per rollout
    /// from `seeds`; the event handlers are ignored (each rollout gets its own quiet pair).
    Options options = Options();
};

/// @brief The outcome of one rollout in the batch.
struct RolloutResult
{
    SearchStatus status = SearchStatus::IN_PROGRESS;

    /// @brief Fluent ground atoms reached by this rollout. Ground-atom indices are stable
    /// across rollouts (the `Problem`'s `Repositories` is frozen during a grounded search),
    /// so these bitsets can be intersected across the batch.
    FlatBitset reached_fluent_atoms = {};
    FlatBitset reached_derived_atoms = {};

    size_t num_states = 0;
};

/// @brief Run one IW rollout per seed in parallel over the shared `Problem` of `context`.
///
/// The `ApplicableActionGenerator` and `AxiomEvaluator` of `context` are shared by every
/// rollout; only the `StateRepository` (and its interning tables) is per-rollout. Results
/// are returned in seed order and are identical to running the same seeds serially.
///
/// @throws std::runtime_error if `context` is not a grounded search context. In lifted mode
/// the `Problem`'s `Repositories` grows during search and is not thread-safe.
extern std::vector<RolloutResult> find_rollouts_parallel(const SearchContext& context, const ParallelRolloutOptions& options);

}

#endif
