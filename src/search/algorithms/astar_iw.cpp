#include "mimir/search/algorithms/astar_iw.hpp"

#include "astar_iw/novelty.hpp"
#include "mimir/common/segmented_vector.hpp"
#include "mimir/common/timers.hpp"
#include "mimir/formalism/ground_function_expressions.hpp"
#include "mimir/formalism/metric.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/astar_iw/event_handlers.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/heuristics/interface.hpp"
#include "mimir/search/openlists/priority_queue.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/search_node.hpp"
#include "mimir/search/search_space.hpp"
#include "mimir/search/state_repository.hpp"

#include <cmath>
#include <tuple>

namespace mimir::search::astar_iw
{
namespace
{

struct SearchNode
{
    ContinuousCost g_value;
    Index parent_state;
    SearchNodeStatus status;
};

using SearchNodeVector = SegmentedVector<SearchNode>;

SearchNode& get_or_create_search_node(size_t state_index, SearchNodeVector& search_nodes)
{
    static constexpr auto default_node = SearchNode { INFINITY_CONTINUOUS_COST, MAX_INDEX, SearchNodeStatus::NEW };
    while (state_index >= search_nodes.size())
    {
        search_nodes.push_back(default_node);
    }
    return search_nodes[state_index];
}

struct QueueEntry
{
    using KeyType = std::tuple<ContinuousCost, ContinuousCost, ContinuousCost, Index>;
    using ItemType = std::tuple<ContinuousCost, PackedState, bool>;

    ContinuousCost f_value;
    ContinuousCost h_value;
    ContinuousCost g_value;
    PackedState packed_state;
    Index state_index;
    bool root_successor;

    KeyType get_key() const { return { f_value, h_value, g_value, state_index }; }
    ItemType get_item() const { return { g_value, packed_state, root_successor }; }
};

using Queue = PriorityQueue<QueueEntry>;

void validate_options(const Options& options)
{
    if (!std::isfinite(options.heuristic_weight) || options.heuristic_weight < 0)
    {
        throw std::invalid_argument("AStarIW heuristic_weight must be finite and nonnegative.");
    }
    if (options.novelty_feature_mode == NoveltyFeatureMode::CLASSICAL)
    {
        if (options.width < 1 || options.width >= iw::MAX_ARITY)
        {
            throw std::invalid_argument("AStarIW classical width must be in [1, iw::MAX_ARITY).");
        }
    }
    else if (options.width < 1 || options.width > 3)
    {
        throw std::invalid_argument("AStarIW abstracted width must be in {1, 2, 3}.");
    }
}

}

SearchResult find_solution(const SearchContext& context, const Heuristic& heuristic, const Options& options)
{
    assert(context && heuristic);
    validate_options(options);

    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();
    const auto [start_state, start_g_value] = options.start_state ? std::make_pair(*options.start_state, compute_state_metric_value(*options.start_state)) :
                                                                    state_repository.get_or_create_initial_state();
    const auto event_handler = options.event_handler ? options.event_handler : DefaultEventHandlerImpl::create(context->get_problem());
    const auto goal_strategy = options.goal_strategy ? options.goal_strategy : ProblemGoalStrategyImpl::create(context->get_problem());

    auto result = SearchResult {};
    auto search_nodes = SearchNodeVector {};

    if (!goal_strategy->test_static_goal())
    {
        event_handler->on_unsolvable();
        result.status = SearchStatus::UNSOLVABLE;
        return result;
    }

    if (goal_strategy->test_dynamic_goal(start_state))
    {
        result.plan = Plan(context, StateList { start_state }, formalism::GroundActionList {}, start_g_value);
        result.goal_state = start_state;
        result.status = SearchStatus::SOLVED;
        event_handler->on_solved(*result.plan);
        return result;
    }

    if (std::isnan(start_g_value))
    {
        throw std::runtime_error("AStarIW initial g value is NaN.");
    }
    const auto start_h_value = heuristic->compute_heuristic(start_state);
    if (std::isnan(start_h_value))
    {
        throw std::runtime_error("AStarIW initial heuristic value is NaN.");
    }
    if (start_h_value == INFINITY_CONTINUOUS_COST)
    {
        event_handler->on_unsolvable();
        result.status = SearchStatus::UNSOLVABLE;
        return result;
    }

    auto novelty = MinimumGNoveltyBackend(context->get_problem(),
                                          options.novelty_feature_mode,
                                          options.width,
                                          options.preserve_goal_atoms,
                                          options.landmark_novelty_graph);
    novelty.initialize(start_state, start_g_value);

    auto& start_node = get_or_create_search_node(start_state.get_index(), search_nodes);
    start_node.g_value = start_g_value;
    start_node.status = SearchNodeStatus::OPEN;

    const auto start_f_value = start_g_value + options.heuristic_weight * start_h_value;
    event_handler->on_start_search(start_state, start_g_value, start_f_value);

    auto openlist = Queue {};
    openlist.insert(QueueEntry { start_f_value, start_h_value, start_g_value, start_state.get_packed_state(), start_state.get_index(), false });

    auto stopwatch = StopWatch(options.max_time_in_ms);
    stopwatch.start();

    const auto finish_search = [&]()
    {
        event_handler->on_end_search(state_repository.get_state_count(), search_nodes.size());
        applicable_action_generator.on_end_search();
        state_repository.get_axiom_evaluator()->on_end_search();
    };

    while (!openlist.empty())
    {
        if (stopwatch.has_finished())
        {
            result.status = SearchStatus::OUT_OF_TIME;
            finish_search();
            return result;
        }

        const auto [entry_g_value, packed_state, is_root_successor] = openlist.top();
        openlist.pop();
        const auto state = state_repository.get_state(*packed_state);
        auto& search_node = get_or_create_search_node(state.get_index(), search_nodes);

        if (entry_g_value != search_node.g_value || search_node.status == SearchNodeStatus::DEAD_END || search_node.status == SearchNodeStatus::CLOSED)
        {
            event_handler->on_discard_stale_g(state);
            continue;
        }

        const auto is_goal = goal_strategy->test_dynamic_goal(state);
        if (is_root_successor && options.allow_non_novel_root_goal && is_goal)
        {
            event_handler->on_expand_goal_state(state);
            result.plan = extract_total_ordered_plan(start_state, start_g_value, search_node, state.get_index(), search_nodes, context);
            result.goal_state = state;
            result.status = SearchStatus::SOLVED;
            finish_search();
            event_handler->on_solved(*result.plan);
            return result;
        }

        /* The stale-novelty test costs a full tuple enumeration per expansion, and it can
           only fail for a state that no longer owns any tuple at its own g value. Every
           state reaches the queue by lowering some tuple to exactly that g, so until some
           already-set label is lowered again -- which never happens while the queue pops in
           non-decreasing g, i.e. the blind case -- the test cannot fail and can be skipped.
           Root successors are the exception: `allow_non_novel_root_goal` admits them
           without any tuple of their own, so they are always tested. */
        const auto admitted_without_novelty = is_root_successor && options.allow_non_novel_root_goal;
        if ((admitted_without_novelty || novelty.may_have_stale_novelty()) && state.get_index() != start_state.get_index()
            && !novelty.test_at_g(state, entry_g_value))
        {
            search_node.status = SearchNodeStatus::CLOSED;
            event_handler->on_discard_stale_novelty(state);
            continue;
        }

        if (is_goal)
        {
            event_handler->on_expand_goal_state(state);
            result.plan = extract_total_ordered_plan(start_state, start_g_value, search_node, state.get_index(), search_nodes, context);
            result.goal_state = state;
            result.status = SearchStatus::SOLVED;
            finish_search();
            event_handler->on_solved(*result.plan);
            return result;
        }

        event_handler->on_expand_state(state);
        search_node.status = SearchNodeStatus::CLOSED;

        for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
        {
            assert(is_applicable(action, state));
            const auto [successor_state, successor_g_value] = state_repository.get_or_create_successor_state(state, action, search_node.g_value);
            const auto action_cost = successor_g_value - search_node.g_value;
            if (action_cost != ContinuousCost(1))
            {
                throw std::invalid_argument("AStarIW requires every generated action to have unit cost.");
            }
            if (std::isnan(successor_g_value))
            {
                throw std::runtime_error("AStarIW successor g value is NaN.");
            }

            event_handler->on_generate_state(state, action, action_cost, successor_state);
            auto& successor_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
            const auto was_new = successor_node.status == SearchNodeStatus::NEW;
            const auto was_closed = successor_node.status == SearchNodeStatus::CLOSED;

            if (was_new && search_nodes.size() >= options.max_num_states)
            {
                result.status = SearchStatus::OUT_OF_STATES;
                finish_search();
                return result;
            }
            if (!(successor_g_value < successor_node.g_value))
            {
                continue;
            }

            successor_node.g_value = successor_g_value;
            successor_node.parent_state = state.get_index();

            /* Novelty is tested before the heuristic is evaluated, because most generated
               successors are rejected by it and a heuristic evaluation is by far the more
               expensive of the two. The test is split into a read-only probe and the commit
               below so the table still sees exactly the states it saw before: a successor
               that fails novelty writes nothing (its update never lowered anything anyway),
               and one that turns out to be a dead end writes nothing either. */
            const auto root_successor = state.get_index() == start_state.get_index();
            const auto novelty_exempt = root_successor && options.allow_non_novel_root_goal;
            if (options.probe_novelty_before_heuristic && !novelty_exempt
                && !novelty.would_improve(state, successor_state, successor_g_value))
            {
                successor_node.status = SearchNodeStatus::CLOSED;
                event_handler->on_reject_state_novelty(successor_state);
                continue;
            }

            const auto successor_h_value = heuristic->compute_heuristic(successor_state);
            if (std::isnan(successor_h_value))
            {
                throw std::runtime_error("AStarIW successor heuristic value is NaN.");
            }
            if (successor_h_value == INFINITY_CONTINUOUS_COST)
            {
                successor_node.status = SearchNodeStatus::DEAD_END;
                event_handler->on_deadend_state(successor_state);
                continue;
            }

            const auto improves_novelty = novelty.test_and_update(state, successor_state, successor_g_value);
            if (!improves_novelty && !novelty_exempt)
            {
                successor_node.status = SearchNodeStatus::CLOSED;
                event_handler->on_reject_state_novelty(successor_state);
                continue;
            }

            if (was_closed)
            {
                event_handler->on_reopen_state(successor_state);
            }
            successor_node.status = SearchNodeStatus::OPEN;
            const auto successor_f_value = successor_g_value + options.heuristic_weight * successor_h_value;
            openlist.insert(QueueEntry { successor_f_value,
                                         successor_h_value,
                                         successor_g_value,
                                         successor_state.get_packed_state(),
                                         successor_state.get_index(),
                                         root_successor });
        }
    }

    finish_search();
    event_handler->on_exhausted();
    result.status = SearchStatus::EXHAUSTED;
    return result;
}

}
