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

#include "mimir/search/algorithms/iw/atomic_goal_portfolio.hpp"

#include "mimir/algorithms/BS_thread_pool.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/ground_literal.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/search_control.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/applicable_action_generators/grounded/grounded.hpp"
#include "mimir/search/applicable_action_generators/lifted/kpkc.hpp"
#include "mimir/search/axiom_evaluators/grounded/grounded.hpp"
#include "mimir/search/axiom_evaluators/lifted/kpkc.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace mimir::formalism;

namespace mimir::search::iw
{

namespace
{

/// @brief The start state as content rather than identity.
///
/// A `State` belongs to the repository that created it -- its packed form is a `valla::Slot` into
/// that repository's interning tables, and its unpacked handle carries a non-atomic refcount into
/// that repository's pool. Neither survives crossing into a worker. Ground atom indices do: they are
/// stable in the parent problem and resolve unchanged inside every overlay.
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

/// @brief The best plan found so far, in the one representation that may cross a thread boundary.
///
/// Monotone by construction: the length is compared and the plan stored under the same lock, so a
/// worker that wins the comparison cannot have its plan overwritten by a longer one that raced past
/// it. The atomic in `SearchControl` is only a hint for the lock-free readers on the hot paths; this
/// mutex is the authority.
class Incumbent
{
private:
    mutable std::mutex m_mutex;
    rollout_iw::PlanStepList m_plan_steps;
    uint32_t m_length = std::numeric_limits<uint32_t>::max();
    uint32_t m_source_worker = std::numeric_limits<uint32_t>::max();
    uint64_t m_version = 0;

public:
    /// @return true if this call installed a new best plan.
    bool publish(const rollout_iw::PlanStepList& plan_steps, uint32_t worker, SearchControl& control)
    {
        const auto candidate_length = static_cast<uint32_t>(plan_steps.size());

        auto lock = std::lock_guard(m_mutex);
        if (candidate_length >= m_length)
        {
            return false;
        }

        m_plan_steps = plan_steps;
        m_length = candidate_length;
        m_source_worker = worker;
        ++m_version;
        control.incumbent_length.store(m_length, std::memory_order_relaxed);
        return true;
    }

    struct Snapshot
    {
        rollout_iw::PlanStepList plan_steps;
        uint32_t length = std::numeric_limits<uint32_t>::max();
        uint32_t source_worker = std::numeric_limits<uint32_t>::max();
        uint64_t version = 0;
    };

    Snapshot read() const
    {
        auto lock = std::lock_guard(m_mutex);
        return Snapshot { m_plan_steps, m_length, m_source_worker, m_version };
    }
};

/// @brief Everything one search worker owns exclusively.
struct Worker
{
    Problem problem;                       ///< the parent in grounded mode, an overlay in lifted mode
    ApplicableActionGenerator generator;   ///< shared in grounded mode, private in lifted mode
    AxiomEvaluator axiom_evaluator;
    StateRepository state_repository;
    SearchContext context;
    GoalStrategy goal_strategy;            ///< one per worker: `IGoalStrategy` methods are non-const
    State start_state;
};

bool is_grounded_context(const SearchContext& context)
{
    return std::dynamic_pointer_cast<GroundedApplicableActionGeneratorImpl>(context->get_applicable_action_generator())
           && std::dynamic_pointer_cast<GroundedAxiomEvaluatorImpl>(context->get_state_repository()->get_axiom_evaluator());
}

bool is_lifted_kpkc_context(const SearchContext& context)
{
    return std::dynamic_pointer_cast<KPKCLiftedApplicableActionGeneratorImpl>(context->get_applicable_action_generator())
           && std::dynamic_pointer_cast<KPKCLiftedAxiomEvaluatorImpl>(context->get_state_repository()->get_axiom_evaluator());
}

AtomicGoalPortfolioSearchMode resolve_mode(const SearchContext& context, AtomicGoalPortfolioSearchMode requested)
{
    switch (requested)
    {
        case AtomicGoalPortfolioSearchMode::GROUNDED:
            if (!is_grounded_context(context))
            {
                throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: GROUNDED mode requires a grounded search context.");
            }
            return AtomicGoalPortfolioSearchMode::GROUNDED;

        case AtomicGoalPortfolioSearchMode::LIFTED_KPKC:
            if (!is_lifted_kpkc_context(context))
            {
                throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: LIFTED_KPKC mode requires a lifted KPKC search context. The "
                                         "exhaustive lifted generators are not supported.");
            }
            return AtomicGoalPortfolioSearchMode::LIFTED_KPKC;

        case AtomicGoalPortfolioSearchMode::INHERIT_CONTEXT:
            if (is_grounded_context(context))
            {
                return AtomicGoalPortfolioSearchMode::GROUNDED;
            }
            if (is_lifted_kpkc_context(context))
            {
                return AtomicGoalPortfolioSearchMode::LIFTED_KPKC;
            }
            throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: the given search context is neither grounded nor lifted KPKC. Pre-grounding "
                                     "it here is exactly what lifted mode exists to avoid, so there is no fallback.");
    }

    throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: unknown AtomicGoalPortfolioSearchMode.");
}

/// @brief The built-in mix used when the caller does not name orderings.
///
/// Deliberately diverse: two goal-directed strategies to head straight for the goal when the domain
/// rewards it, and randomized guidance so the portfolio still makes progress when it does not.
rollout_iw::ActionOrderingConfiguration default_ordering_for(uint32_t worker_offset, uint64_t base_seed)
{
    static constexpr rollout_iw::ActionOrderingKind kCycle[] = { rollout_iw::ActionOrderingKind::DIRECT_GOAL_ACHIEVER_FIRST,
                                                                rollout_iw::ActionOrderingKind::MIXED_REGRESSION_RANDOM,
                                                                rollout_iw::ActionOrderingKind::RANDOMIZED,
                                                                rollout_iw::ActionOrderingKind::GOAL_REGRESSION_RELEVANCE };

    return rollout_iw::ActionOrderingConfiguration(kCycle[worker_offset % (sizeof(kCycle) / sizeof(kCycle[0]))], base_seed + worker_offset);
}

std::string describe(SearchStatus status)
{
    switch (status)
    {
        case SearchStatus::IN_PROGRESS:
            return "in progress";
        case SearchStatus::OUT_OF_TIME:
            return "out of time";
        case SearchStatus::OUT_OF_MEMORY:
            return "out of memory";
        case SearchStatus::OUT_OF_STATES:
            return "out of states";
        case SearchStatus::FAILED:
            return "failed";
        case SearchStatus::EXHAUSTED:
            return "exhausted";
        case SearchStatus::SOLVED:
            return "solved";
        case SearchStatus::UNSOLVABLE:
            return "unsolvable";
        case SearchStatus::CANCELED:
            return "canceled";
    }
    return "unknown";
}

}

AtomicGoalPortfolioResult find_solution_atomic_goal_portfolio(const SearchContext& context, const AtomicGoalPortfolioOptions& options)
{
    /* ------------------------------------------------------------------------------------------
       1. Validation. Everything that can fail is made to fail here, on the calling thread, because
          an exception escaping a pool task calls std::terminate.
       ------------------------------------------------------------------------------------------ */

    if (!context)
    {
        throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: context must not be null.");
    }

    const auto mode = resolve_mode(context, options.search_mode);
    const auto parent = context->get_problem();

    if (parent->is_grounding_overlay())
    {
        throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: the given context is itself built on a grounding overlay. Overlays are "
                                 "per-worker workspaces and must not be shared or layered.");
    }

    if (options.start_state && (&options.start_state->get_problem() != parent.get()))
    {
        throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: the given start state belongs to a different problem than the context.");
    }

    if (options.atomic_goal && options.atomic_goal.value() == nullptr)
    {
        throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: atomic_goal was set but null.");
    }

    /* Canonicalize the goal once, in the parent, before any overlay exists. Identity of a
       `GroundConjunctiveCondition` is the identity of the six flat index lists behind it, which are
       interned per problem -- so building it inside a worker would give every worker a different
       object. Interned in the parent it is shared, read-only, and its atom indices resolve in every
       overlay. This is also the last time the parent is written to until finalization. */
    const auto atomic_goal = [&]() -> GroundConjunctiveCondition
    {
        if (options.atomic_goal)
        {
            return options.atomic_goal.value();
        }
        if (options.atomic_goal_atoms.empty())
        {
            return parent->get_goal_condition();
        }

        auto literals = GroundLiteralLists<StaticTag, FluentTag, DerivedTag> {};
        auto& fluent_literals = boost::hana::at_key(literals, boost::hana::type<FluentTag> {});
        for (const auto& atom : options.atomic_goal_atoms)
        {
            if (!atom)
            {
                throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: atomic_goal_atoms must not contain null atoms.");
            }
            fluent_literals.push_back(parent->get_or_create_ground_literal<FluentTag>(true, atom));
        }
        return parent->get_or_create_ground_conjunctive_condition(std::move(literals), GroundNumericConstraintList {});
    }();

    auto result = AtomicGoalPortfolioResult {};
    result.executed_mode = mode;

    /* A statically unsatisfiable goal is unsolvable regardless of search, and finding that out here
       avoids spawning K+1 workers to each rediscover it. */
    if (!ProblemGoalStrategyImpl::create(parent, atomic_goal)->test_static_goal())
    {
        result.status = SearchStatus::UNSOLVABLE;
        result.stop_reason = "the atomic goal's static part cannot hold";
        return result;
    }

    /* Capture the start state as content before anything is copied per worker. Keeping the caller's
       `State` in the options and copying those options K+1 times would race its pooled refcount
       K+1 ways -- see the note on `DenseStartState` and commit 09cbe5b5a. */
    const auto dense_start_state =
        options.start_state ? describe_start_state(options.start_state.value()) :
                              DenseStartState { parent->get_fluent_initial_atoms(), parent->get_initial_function_to_value<FluentTag>() };

    const auto num_rollout_workers = options.num_rollout_workers;
    const auto num_workers = size_t(1) + num_rollout_workers;  ///< worker 0 is the certifier

    result.rollout_statistics.resize(num_rollout_workers);
    result.rollout_statuses.assign(num_rollout_workers, SearchStatus::IN_PROGRESS);

    /* ------------------------------------------------------------------------------------------
       2. Build the workers. Each gets everything it will mutate, and nothing else.
       ------------------------------------------------------------------------------------------ */

    auto shared_generator = context->get_applicable_action_generator();
    auto shared_axiom_evaluator = context->get_state_repository()->get_axiom_evaluator();

    auto workers = std::vector<Worker> {};
    workers.reserve(num_workers);

    for (size_t k = 0; k < num_workers; ++k)
    {
        auto problem = Problem {};
        auto generator = ApplicableActionGenerator {};
        auto axiom_evaluator = AxiomEvaluator {};
        auto state_repository = StateRepository {};

        if (mode == AtomicGoalPortfolioSearchMode::GROUNDED)
        {
            /* The grounded generators traverse a pre-built match tree and leave the problem's
               repositories frozen, so all workers can share them. Only the state repository has to
               be private, and it gets its own interning tables so the workers never contend. */
            problem = parent;
            generator = shared_generator;
            axiom_evaluator = shared_axiom_evaluator;
            state_repository = StateRepositoryImpl::create(shared_axiom_evaluator, StateRepositoryImpl::PrivateInterningTables {});
        }
        else
        {
            /* Lifted: every worker grounds on demand, so every worker needs its own place to ground
               into. Symmetry pruning is off (D9): the certifier's shortest-plan claim is only sound
               over the complete transition system, and partial-binding completion needs it off too.
               The overlay owns its interning tables, so the state repository needs no separate ones.
               `prepare_parallel_*` is deliberately never called -- those pre-ground the type-legal
               atom universe, which is the whole thing lifted mode exists to avoid. */
            problem = ProblemImpl::create_grounding_overlay(parent);
            generator = std::make_shared<KPKCLiftedApplicableActionGeneratorImpl>(
                problem,
                SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::OFF));
            axiom_evaluator = std::make_shared<KPKCLiftedAxiomEvaluatorImpl>(problem);
            state_repository = StateRepositoryImpl::create(axiom_evaluator);
        }

        auto worker_context = SearchContextImpl::create(problem, generator, state_repository);

        /* One strategy instance per worker: `IGoalStrategy`'s methods are non-const virtuals. The
           condition itself is the parent's and is only read. */
        auto goal_strategy = GoalStrategy(ProblemGoalStrategyImpl::create(problem, atomic_goal));

        /* Re-created here so that returning to the start state is recognized as a duplicate by this
           worker's own repository. */
        auto start_state = state_repository->get_or_create_state(dense_start_state.fluent_atoms, dense_start_state.numeric_variables).first;

        workers.push_back(Worker { std::move(problem),
                                   std::move(generator),
                                   std::move(axiom_evaluator),
                                   std::move(state_repository),
                                   std::move(worker_context),
                                   std::move(goal_strategy),
                                   std::move(start_state) });
    }

    /* ------------------------------------------------------------------------------------------
       3. Run.
       ------------------------------------------------------------------------------------------ */

    auto control = SearchControl();
    /* Hand the cap to the workers rather than only policing it from here: the serial path cannot
       interrupt a worker mid-search, and the parallel path only notices between 5 ms polls. */
    control.max_total_expansions.store(options.max_total_expansions, std::memory_order_relaxed);
    auto incumbent = Incumbent();
    auto certifier_event_handler = brfs::DefaultEventHandlerImpl::create(parent, true);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.max_time_in_ms);
    const auto remaining_time_in_ms = [&]() -> uint32_t
    {
        if (options.max_time_in_ms == std::numeric_limits<uint32_t>::max())
        {
            return std::numeric_limits<uint32_t>::max();
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        return (remaining <= 0) ? 0u : static_cast<uint32_t>(remaining);
    };

    auto certifier_status = SearchStatus::IN_PROGRESS;

    const auto run_certifier = [&]()
    {
        auto certifier_options = Options();
        certifier_options.start_state = workers[0].start_state;
        certifier_options.goal_strategy = workers[0].goal_strategy;
        certifier_options.max_arity = 1;  ///< canonical IW(1): the width the certificate is about
        certifier_options.max_depth = options.max_depth;
        certifier_options.max_time_in_ms = remaining_time_in_ms();
        certifier_options.control = &control;
        /* Quiet, native handlers only. The shared grounded generator's own handler is written by
           every worker, so anything but a no-op there is a genuine data race on statistics. */
        certifier_options.iw_event_handler = DefaultEventHandlerImpl::create(workers[0].problem, true);
        certifier_options.brfs_event_handler = certifier_event_handler;

        const auto search_result = find_solution(workers[0].context, certifier_options);
        certifier_status = search_result.status;

        if ((search_result.status == SearchStatus::SOLVED) && search_result.plan.has_value())
        {
            auto steps = rollout_iw::PlanStepList {};
            for (const auto& action : search_result.plan->get_actions())
            {
                steps.push_back(rollout_iw::PlanStep { action->get_action(), action->get_objects() });
            }
            incumbent.publish(steps, 0, control);

            /* Breadth-first with the goal tested on pop: the first goal popped is at the minimum
               depth of the width-1 space, so nothing shorter exists there and there is nothing left
               for anyone to find. */
            control.request_cancel();
        }
        else
        {
            /* Even without a plan of its own the certifier may have proven the incumbent optimal by
               completing enough layers. */
            if (control.is_incumbent_certified(control.get_incumbent_length()))
            {
                control.request_cancel();
            }
        }
    };

    const auto run_rollout_worker = [&](size_t k)
    {
        const auto offset = static_cast<uint32_t>(k - 1);
        const auto configuration = options.rollout_orderings.empty() ? default_ordering_for(offset, options.base_seed) :
                                                                       options.rollout_orderings[offset % options.rollout_orderings.size()];

        auto rollout_options = rollout_iw::Options();
        rollout_options.start_state = workers[k].start_state;
        rollout_options.goal_strategy = workers[k].goal_strategy;
        rollout_options.action_ordering = rollout_iw::create_action_ordering_strategy(configuration, workers[k].problem, atomic_goal);
        rollout_options.seed = configuration.seed;
        rollout_options.max_depth = options.max_depth;
        rollout_options.max_time_in_ms = remaining_time_in_ms();
        rollout_options.control = &control;

        for (;;)
        {
            const auto bound_before_round = control.get_incumbent_length();

            const auto search_result = rollout_iw::find_solution(workers[k].context, rollout_options);

            /* Accumulated across restarts, so the reported numbers are the work this worker did, not
               just what its last -- often immediately canceled -- round happened to do. */
            auto& statistics = result.rollout_statistics[offset];
            statistics.num_rollouts += search_result.statistics.num_rollouts;
            statistics.num_generated_states += search_result.statistics.num_generated_states;
            statistics.num_expanded_nodes += search_result.statistics.num_expanded_nodes;
            statistics.num_feature_depth_improvements += search_result.statistics.num_feature_depth_improvements;
            statistics.num_case_1 += search_result.statistics.num_case_1;
            statistics.num_case_2 += search_result.statistics.num_case_2;
            statistics.num_case_3 += search_result.statistics.num_case_3;
            statistics.num_case_4 += search_result.statistics.num_case_4;
            statistics.num_solved_propagations += search_result.statistics.num_solved_propagations;
            statistics.num_dead_ends += search_result.statistics.num_dead_ends;
            statistics.num_depth_bound_prunings += search_result.statistics.num_depth_bound_prunings;
            statistics.num_incumbent_bound_prunings += search_result.statistics.num_incumbent_bound_prunings;
            statistics.num_tree_nodes += search_result.statistics.num_tree_nodes;
            statistics.max_rollout_depth = std::max(statistics.max_rollout_depth, search_result.statistics.max_rollout_depth);

            result.rollout_statuses[offset] = search_result.search_result.status;

            if (search_result.search_result.status == SearchStatus::SOLVED)
            {
                incumbent.publish(search_result.plan_steps, static_cast<uint32_t>(k), control);

                if (control.is_incumbent_certified(control.get_incumbent_length()))
                {
                    control.request_cancel();
                    return;
                }

                /* Found something, but nobody has proven it shortest yet. Restart under the tighter
                   bound and look for something better; the bound makes each restart cheaper than the
                   last. `rollout_iw` reads `control->incumbent_length` on its own, so this picks up
                   improvements from other workers too. */
                if (control.is_canceled() || (remaining_time_in_ms() == 0))
                {
                    return;
                }

                /* Only restart if the bound this round will run under is strictly tighter than the
                   one the round just finished under. Without this the loop can spin forever: the
                   incumbent bound prunes a node by what its *children* would cost, so it never
                   prunes the root's own children, and the goal is tested before any bound
                   reasoning. A depth-1 goal is therefore immune to the bound, and a worker holding
                   a length-1 incumbent re-finds the very same plan on every restart -- publishing
                   nothing, certifying nothing, and never exiting on its own. Re-reading the bound
                   rather than just checking `publish`'s verdict keeps the restart that another
                   worker's improvement genuinely earns. */
                if (control.get_incumbent_length() >= bound_before_round)
                {
                    return;
                }

                rollout_options.max_time_in_ms = remaining_time_in_ms();
                continue;
            }

            return;  ///< exhausted, canceled, or out of budget: nothing more this worker can do
        }
    };

    const auto run_worker = [&](size_t k)
    {
        if (k == 0)
        {
            run_certifier();
        }
        else
        {
            run_rollout_worker(k);
        }
    };

    const auto hardware_threads = std::max<uint32_t>(1, std::thread::hardware_concurrency());
    const auto num_threads = std::min<size_t>(num_workers, (options.num_threads == 0) ? hardware_threads : options.num_threads);

    if (num_threads <= 1)
    {
        /* Serial: run every worker to completion in order, on the calling thread. The certifier goes
           first so that a plan it finds cancels the rest immediately. */
        for (size_t k = 0; k < num_workers; ++k)
        {
            if (control.is_canceled() || (remaining_time_in_ms() == 0) || (control.get_total_expansions() >= options.max_total_expansions))
            {
                control.request_cancel();
                break;
            }
            run_worker(k);
        }
    }
    else
    {
        auto pool = BS::thread_pool(static_cast<BS::concurrency_t>(num_threads));

        /* `submit_task` captures an escaping exception into the future; the detached variants would
           let it reach std::terminate (the pool is built with exception handling disabled). */
        auto futures = std::vector<std::future<void>> {};
        futures.reserve(num_workers);
        for (size_t k = 0; k < num_workers; ++k)
        {
            /* A worker that throws must cancel its siblings before its exception is parked in the
               future. The coordinator only rethrows once every future is ready, so an unbounded
               sibling would otherwise keep the whole run -- and the exception -- pending forever. */
            futures.push_back(pool.submit_task(
                [&run_worker, &control, k]
                {
                    try
                    {
                        run_worker(k);
                    }
                    catch (...)
                    {
                        control.request_cancel();
                        throw;
                    }
                }));
        }

        /* Poll rather than plain-wait, so budgets the workers cannot see for themselves -- the
           shared expansion budget, and the deadline when a worker is stuck inside one enormous KPKC
           enumeration -- still stop the run. */
        auto pending = num_workers;
        while (pending > 0)
        {
            pending = 0;
            for (auto& future : futures)
            {
                if (future.valid() && (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready))
                {
                    ++pending;
                }
            }

            if (pending == 0)
            {
                break;
            }

            if (control.get_total_expansions() >= options.max_total_expansions)
            {
                control.request_cancel();
            }
            if (remaining_time_in_ms() == 0)
            {
                control.request_cancel();
            }
        }

        auto first_exception = std::exception_ptr {};
        for (auto& future : futures)
        {
            try
            {
                future.get();
            }
            catch (...)
            {
                if (!first_exception)
                {
                    first_exception = std::current_exception();
                }
            }
        }
        if (first_exception)
        {
            std::rethrow_exception(first_exception);
        }
    }

    /* ------------------------------------------------------------------------------------------
       4. Finalize. Every worker has joined, so the parent may grow again -- and this is the only
          place it does. From here on no overlay index is ever resolved, which is what makes growing
          it safe: an overlay's local indices are stamped relative to the parent's size.
       ------------------------------------------------------------------------------------------ */

    const auto snapshot = incumbent.read();

    /* Drop the workers -- and with them the overlays, their state repositories and the states those
       hold -- before the parent is allowed to grow. An overlay's local indices are stamped as
       `parent->size() + local_offset`, so any index resolved through one after this point would be
       wrong; destroying them here turns "nobody resolves an overlay index again" from a property of
       the code below into something the code cannot violate. Safe on this thread and only on this
       thread: every worker has joined, and a `State`'s pooled handle is not refcounted atomically. */
    workers.clear();

    result.certifier_status = certifier_status;
    result.iw_statistics = certifier_event_handler->get_statistics();
    result.iw_completed_depth = control.get_completed_depth();
    result.iw_lower_bound = control.get_lower_bound();
    result.total_expansions = control.get_total_expansions();

    if (snapshot.length == std::numeric_limits<uint32_t>::max())
    {
        /* Nobody found anything. Say why, honestly: exhaustion is a proof of unsolvability within
           the width-1 space, a budget is not a proof of anything. */
        if (certifier_status == SearchStatus::UNSOLVABLE)
        {
            result.status = SearchStatus::UNSOLVABLE;
            result.stop_reason = "the certifier proved the goal unreachable";
        }
        else if (certifier_status == SearchStatus::EXHAUSTED)
        {
            result.status = SearchStatus::EXHAUSTED;
            result.stop_reason = "the certifier exhausted the width-1 space without finding a plan";
        }
        else
        {
            result.status = certifier_status;
            result.stop_reason = "no plan found; certifier stopped: " + describe(certifier_status);
        }
        return result;
    }

    result.plan_steps = snapshot.plan_steps;
    result.plan_length = snapshot.length;
    result.winning_worker = snapshot.source_worker;
    result.status = SearchStatus::SOLVED;

    /* Certified either because the certifier found this plan itself -- breadth-first, goal tested on
       pop, so nothing shorter exists in the width-1 space -- or because it completed every layer up
       to `plan_length - 1` without finding a goal there.

       With one exception. If the certifier searched its whole space and came back with nothing while
       somebody else found a plan, that plan demonstrably lies *outside* the space the certificate is
       about, and the certificate therefore says nothing about it.

       That is not a corner case to wave away. Width-1 novelty pruning does keep the node that first
       makes an atom true -- but only if that node is generated at all, which needs its whole
       ancestry to have survived pruning too. IW(1) is complete and optimal exactly for goals of
       width at most 1, which is the regime this portfolio is built for; hand it a goal of higher
       width (any conjunctive goal, and plenty of single atoms) and the certifier can come back
       empty while a rollout worker, whose feature-depth bookkeeping prunes differently, does not. */
    const auto certifier_searched_its_whole_space = (certifier_status == SearchStatus::EXHAUSTED) || (certifier_status == SearchStatus::FAILED);
    const auto incumbent_is_outside_the_certified_space = certifier_searched_its_whole_space && (snapshot.source_worker != 0);

    result.certified_optimal = (snapshot.source_worker == 0) || (control.is_incumbent_certified(snapshot.length) && !incumbent_is_outside_the_certified_space);

    if (result.certified_optimal)
    {
        result.stop_reason = "certified shortest under width-1 assumptions";
    }
    else if (incumbent_is_outside_the_certified_space)
    {
        result.stop_reason = "best plan found by a rollout worker; the certifier found no plan in the width-1 space at all, so its bound does not apply";
    }
    else
    {
        result.stop_reason = "best plan found; optimality not certified (certifier stopped: " + describe(certifier_status) + ")";
    }

    /* Re-ground the winning schema/binding sequence into the caller's problem and replay it through
       the caller's own state repository, so the returned `Plan` is made of objects the caller can
       actually use. Only O(plan length) actions are grounded, which is affordable even where
       exhaustive grounding is not. */
    auto& caller_state_repository = *context->get_state_repository();
    auto actions = GroundActionList {};
    auto states = StateList {};

    auto [state, metric_value] =
        options.start_state ? std::make_pair(options.start_state.value(), compute_state_metric_value(options.start_state.value())) :
                              caller_state_repository.get_or_create_state(dense_start_state.fluent_atoms, dense_start_state.numeric_variables);
    states.push_back(state);

    for (const auto& step : result.plan_steps)
    {
        const auto action = parent->ground(step.schema, step.binding);

        if (!is_applicable(action, state))
        {
            throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: a step of the winning plan is not applicable when replayed in the "
                                     "caller's problem. This means a worker's schema/binding transport does not describe the transition it took.");
        }

        const auto successor = caller_state_repository.get_or_create_successor_state(state, action, metric_value);
        state = successor.first;
        metric_value = successor.second;

        actions.push_back(action);
        states.push_back(state);
    }

    if (!ProblemGoalStrategyImpl::create(parent, atomic_goal)->test_dynamic_goal(state))
    {
        throw std::runtime_error("iw::find_solution_atomic_goal_portfolio: the winning plan does not reach the atomic goal when replayed in the "
                                 "caller's problem.");
    }

    result.plan = Plan(context, std::move(states), std::move(actions), metric_value);
    return result;
}

}
