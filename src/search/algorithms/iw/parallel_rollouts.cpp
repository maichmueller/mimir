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

#include "mimir/search/algorithms/iw/parallel_rollouts.hpp"

#include "mimir/algorithms/BS_thread_pool.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/applicable_action_generators/grounded/grounded.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/grounded/grounded.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <unordered_map>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace mimir::formalism;

namespace mimir::search::iw
{

namespace
{

/// @brief The dense description of the state every rollout starts from.
///
/// Slot values are local to the interning table that produced them, so a `State` built by
/// the caller's repository cannot be used as a lookup key inside a rollout's private
/// repository. Each rollout therefore re-creates the start state from this dense form; see
/// docs/PARALLEL_IW_ROLLOUTS.md section 3.2.
struct DenseStartState
{
    GroundAtomList<FluentTag> fluent_atoms;
    FlatDoubleList numeric_variables;
};

DenseStartState describe_start_state(const State& state)
{
    auto result = DenseStartState {};
    result.fluent_atoms = state.get_problem().get_repositories().get_ground_atoms_from_indices<FluentTag>(state.get_atoms<FluentTag>());
    result.numeric_variables = state.get_numeric_variables();
    return result;
}

/// @brief Whether any action is applicable in `state`, without enumerating the rest.
bool has_applicable_action(const ApplicableActionGenerator& generator, const State& state)
{
    auto applicable_actions = generator->create_applicable_action_generator(state);
    return applicable_actions.begin() != applicable_actions.end();
}

/// @brief Dedup key for a landing state: its content, not its (rollout-local) identity.
struct LandingKey
{
    IndexList fluent_atom_indices;
    FlatDoubleList numeric_variables;

    bool operator==(const LandingKey& other) const
    {
        return fluent_atom_indices == other.fluent_atom_indices
               && std::equal(numeric_variables.begin(), numeric_variables.end(), other.numeric_variables.begin(), other.numeric_variables.end());
    }
};

struct LandingKeyHash
{
    size_t operator()(const LandingKey& key) const
    {
        auto seed = key.fluent_atom_indices.size();
        for (const auto index : key.fluent_atom_indices)
        {
            loki::hash_combine(seed, index);
        }
        for (const auto value : key.numeric_variables)
        {
            loki::hash_combine(seed, value);
        }
        return seed;
    }
};

LandingKey describe_landing_state(const LandingState& landing_state)
{
    auto key = LandingKey {};
    for (const auto atom_index : landing_state.fluent_atoms)
    {
        key.fluent_atom_indices.push_back(atom_index);
    }
    key.numeric_variables = landing_state.numeric_variables;
    return key;
}

/// @brief Turn this rollout's per-atom first achievers into distinct landing states.
///
/// Runs on the worker, which is the only thread allowed to touch this rollout's repository
/// or hold its `State`s. Every `State` materialized here is destroyed before returning; only
/// dense descriptions cross the join. See `LandingState`.
void collect_landing_states(StateRepositoryImpl& repository, const ApplicableActionGenerator& generator, RolloutResult& ref_result)
{
    const auto& achiever_by_atom = repository.get_first_achiever_state_by_atom();
    ref_result.landing_state_by_atom.assign(achiever_by_atom.size(), MAX_INDEX);

    /* Many atoms share a landing state -- one state achieves every atom its action added --
       so deduplicate here rather than making the caller do it after the join. */
    auto position_by_state_index = std::unordered_map<Index, Index> {};

    for (size_t atom_index = 0; atom_index < achiever_by_atom.size(); ++atom_index)
    {
        const auto state_index = achiever_by_atom[atom_index];
        if (state_index == MAX_INDEX)
        {
            continue;  ///< never reached by this rollout
        }

        auto [it, inserted] = position_by_state_index.emplace(state_index, static_cast<Index>(ref_result.landing_states.size()));
        if (inserted)
        {
            const auto state = repository.get_state(*repository.get_packed_state(state_index));
            auto landing_state = LandingState {};
            landing_state.fluent_atoms = state.get_atoms<FluentTag>();
            landing_state.numeric_variables = state.get_numeric_variables();
            landing_state.is_direct_dead_end = !has_applicable_action(generator, state);
            ref_result.landing_states.push_back(std::move(landing_state));
        }
        ref_result.landing_state_by_atom[atom_index] = it->second;
    }
}

}

std::vector<RolloutResult> find_rollouts_parallel(const SearchContext& context, const ParallelRolloutOptions& options)
{
    const auto num_rollouts = options.seeds.size();
    if (num_rollouts == 0)
    {
        return {};
    }

    /* Reject lifted contexts. In lifted mode the `Problem`'s `Repositories` grows during
       search through `ProblemImpl::ground(...)`, and `loki::IndexedHashSet` has no
       synchronization at all. Only the grounded generators traverse a pre-built match tree
       and leave `Repositories` frozen. */
    const auto applicable_action_generator = context->get_applicable_action_generator();
    const auto axiom_evaluator = context->get_state_repository()->get_axiom_evaluator();
    if (!std::dynamic_pointer_cast<GroundedApplicableActionGeneratorImpl>(applicable_action_generator)
        || !std::dynamic_pointer_cast<GroundedAxiomEvaluatorImpl>(axiom_evaluator))
    {
        throw std::runtime_error("iw::find_rollouts_parallel: requires a grounded search context. In lifted mode the problem's "
                                 "repositories grow during search and are not thread-safe.");
    }

    /* Every rollout is given a `RandomizedLayerOrderingStrategy`, which is what makes the
       rollouts differ. That strategy cannot score states eagerly, so a beam width would
       throw once the search started -- inside a worker thread, where an escaping exception
       would abort the process. Reject it here, on the calling thread, with a clear message. */
    if (options.options.beam_width != std::numeric_limits<uint32_t>::max())
    {
        throw std::runtime_error("iw::find_rollouts_parallel: beam_width is not supported. Rollouts are randomized via a layer "
                                 "ordering strategy that does not support the eager scoring a beam requires.");
    }

    const auto problem = context->get_problem();

    /* Capture the start state densely on the calling thread, before any worker runs.
       When no start state is given we read the initial atoms straight off the `Problem`
       rather than going through `get_or_create_initial_state()`, so that the batch never
       writes to the shared interning tables at all. */
    const auto dense_start_state = options.options.start_state ?
                                       describe_start_state(options.options.start_state.value()) :
                                       DenseStartState { problem->get_fluent_initial_atoms(), problem->get_initial_function_to_value<FluentTag>() };

    /* Strip the caller's `State` out of the options BEFORE anything is copied per worker.
       `State` holds a `SharedObjectPoolPtr<UnpackedStateImpl>` whose refcount is bumped
       non-atomically and whose pool -- the CALLER's -- has no synchronization. Copying the
       options into N tasks therefore races that refcount N ways: a lost increment drops it
       to zero early, the pool reclaims a live `UnpackedStateImpl`, and it is handed out
       again to a different state, which then reads someone else's dense atoms. The visible
       damage is in the caller's repository, not ours -- states start enumerating actions
       whose own preconditions do not hold.
       Nothing downstream needs it: `dense_start_state` above already captured the content
       on this thread, and every rollout re-creates its own start state below. */
    auto base_options = options.options;
    base_options.start_state = std::nullopt;

    /* One private state repository per rollout, each with its own interning tables but
       sharing the (read-only, thread-safe) match-tree-backed generators. */
    auto contexts = std::vector<SearchContext> {};
    contexts.reserve(num_rollouts);
    for (size_t k = 0; k < num_rollouts; ++k)
    {
        auto state_repository = StateRepositoryImpl::create(axiom_evaluator, StateRepositoryImpl::PrivateInterningTables {});
        // Before any state exists in it -- neither tracker is retroactive.
        if (options.report_landing_states)
        {
            state_repository->enable_first_achiever_tracking();
        }
        if (options.report_co_occurrence)
        {
            state_repository->enable_co_occurrence_tracking();
        }
        contexts.push_back(SearchContextImpl::create(problem, applicable_action_generator, std::move(state_repository)));
    }

    auto results = std::vector<RolloutResult>(num_rollouts);

    /* The vendored BS::thread_pool is built with BS_THREAD_POOL_DISABLE_EXCEPTION_HANDLING,
       so an exception escaping a task calls std::terminate. Capture per rollout instead and
       rethrow the first one on the calling thread. */
    auto exceptions = std::vector<std::exception_ptr>(num_rollouts);

    const auto run_rollout_unguarded = [&](size_t k)
    {
        const auto& rollout_context = contexts[k];
        const auto seed = options.seeds[k];

        auto rollout_options = base_options;  ///< never `options.options`: see the note above
        rollout_options.layer_ordering_strategy = RandomizedLayerOrderingStrategyImpl::create(seed);
        rollout_options.randomize_equal_score_ties = true;
        rollout_options.equal_score_tie_seed = seed;
        // Quiet handlers only: the shared generators' handlers are no-ops when quiet, and a
        // Python-facing handler would defeat releasing the GIL for the whole batch.
        rollout_options.iw_event_handler = DefaultEventHandlerImpl::create(problem, true);
        rollout_options.brfs_event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);
        // Re-create the start state inside this rollout's own repository so that returning
        // to it is recognized as a duplicate.
        rollout_options.start_state = rollout_context->get_state_repository()
                                          ->get_or_create_state(dense_start_state.fluent_atoms, dense_start_state.numeric_variables)
                                          .first;

        const auto search_result = find_solution(rollout_context, rollout_options);

        auto& repository = *rollout_context->get_state_repository();
        auto& result = results[k];
        result.status = search_result.status;
        result.reached_fluent_atoms = repository.get_reached_fluent_ground_atoms_bitset();
        result.reached_derived_atoms = repository.get_reached_derived_ground_atoms_bitset();
        result.num_states = repository.get_state_count();

        if (options.report_landing_states)
        {
            collect_landing_states(repository, applicable_action_generator, result);
        }
        if (options.report_co_occurrence)
        {
            result.co_occurrence_by_atom = repository.get_co_occurrence_by_atom();
        }
    };

    const auto run_rollout = [&](size_t k)
    {
        try
        {
            run_rollout_unguarded(k);
        }
        catch (...)
        {
            exceptions[k] = std::current_exception();
        }
    };

    const auto hardware_threads = std::max<uint32_t>(1, std::thread::hardware_concurrency());
    const auto num_threads =
        std::min<size_t>(num_rollouts, (options.num_threads == 0) ? hardware_threads : options.num_threads);

    if (num_threads <= 1)
    {
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            run_rollout(k);
        }
    }
    else
    {
        auto pool = BS::thread_pool(static_cast<BS::concurrency_t>(num_threads));
        pool.detach_sequence(size_t(0), num_rollouts, run_rollout);
        pool.wait();
    }

    for (const auto& exception : exceptions)
    {
        if (exception)
        {
            std::rethrow_exception(exception);
        }
    }

    return results;
}


std::vector<FlatBitset> intersect_co_occurrence(const std::vector<RolloutResult>& results)
{
    if (results.empty())
    {
        return {};
    }

    auto intersected = results.front().co_occurrence_by_atom;
    for (size_t k = 1; k < results.size(); ++k)
    {
        const auto& rows = results[k].co_occurrence_by_atom;
        for (size_t atom_index = 0; atom_index < intersected.size(); ++atom_index)
        {
            if (atom_index >= rows.size())
            {
                /* Beyond this rollout's largest reached atom, so it saw the atom co-occur with
                   nothing. Clearing rather than leaving the row is the whole point: an atom one
                   rollout never reached must not contribute pairs to a unanimous fold. */
                intersected[atom_index].unset_all();
                continue;
            }
            intersected[atom_index] &= rows[atom_index];
        }
    }
    return intersected;
}

std::vector<std::vector<Index>> migrate_landing_states(const std::vector<RolloutResult>& results, StateRepository& target)
{
    auto migrated = std::vector<std::vector<Index>>(results.size());

    /* One entry per DISTINCT landing state across the whole batch. K differently-seeded
       rollouts overwhelmingly land in the same states, and each miss costs a re-intern
       (`valla::insert_sequence` over the dense atoms), so this is worth the map. */
    auto target_index_by_landing_state = std::unordered_map<LandingKey, Index, LandingKeyHash> {};

    for (size_t k = 0; k < results.size(); ++k)
    {
        const auto& landing_states = results[k].landing_states;
        migrated[k].reserve(landing_states.size());

        for (const auto& landing_state : landing_states)
        {
            auto key = describe_landing_state(landing_state);
            const auto it = target_index_by_landing_state.find(key);
            if (it != target_index_by_landing_state.end())
            {
                migrated[k].push_back(it->second);
                continue;
            }

            /* The only step that needs `target`'s own thread: this inserts into `target` and
               hands back a `State` whose pooled handle has a non-atomic refcount. */
            const auto state = target->get_or_create_state(landing_state.fluent_atoms, landing_state.numeric_variables).first;
            const auto target_index = state.get_index();
            target_index_by_landing_state.emplace(std::move(key), target_index);
            migrated[k].push_back(target_index);
        }
    }

    return migrated;
}

}
