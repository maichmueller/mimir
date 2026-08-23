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

#include "mimir/search/algorithms/rollout_iw.hpp"

#include "mimir/common/timers.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::rollout_iw
{

namespace
{

constexpr uint32_t kInvalidNode = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInfiniteDepth = std::numeric_limits<uint32_t>::max();

/// @brief How often to check budgets while draining an applicable-action enumeration.
///
/// One enormous KPKC enumeration must not delay shutdown, but checking a clock per yielded action
/// would show up in the profile of a search that yields millions of them.
constexpr uint32_t kActionYieldCheckMask = 63u;

enum class Label : uint8_t
{
    OPEN,
    SOLVED,
};

/// @brief A node of the rollout tree.
///
/// The tree is over action *sequences*, not states: two nodes may hold the same state if two
/// distinct paths reach it. That is the algorithm's own structure, not an oversight -- a node's
/// depth is part of its identity, and it is what the feature-depth table is compared against.
///
/// The state is kept packed and unpacked on demand, exactly as BrFS keeps its queue, because a
/// rollout only ever needs the one or two states on the path it is currently walking.
struct Node
{
    PackedState packed_state = nullptr;
    ContinuousCost metric_value = 0;
    uint32_t parent = kInvalidNode;
    uint32_t incoming_action_pos = kInvalidNode;  ///< position in the *parent's* action list
    uint32_t depth = 0;
    Label label = Label::OPEN;

    /// @brief Whether `actions` holds *every* applicable action. SOLVED propagation depends on this
    /// being exact: "all children are solved" is only a proof of exhaustion if all children exist.
    bool actions_materialized = false;

    GroundActionList actions;
    std::vector<uint32_t> children;  ///< parallel to `actions`; `kInvalidNode` means untried
};

/// @brief Why the search loop stopped, when it did not solve or exhaust.
enum class Interrupt : uint8_t
{
    NONE,
    TIME,
    CANCELED,
    STATES,
    ROLLOUTS,
};

class RolloutIWSearch
{
private:
    const SearchContext& m_context;
    const Options& m_options;

    StateRepositoryImpl& m_state_repository;
    IApplicableActionGenerator& m_generator;
    GoalStrategy m_goal_strategy;
    ActionOrderingStrategy m_ordering;

    StopWatch m_stopwatch;
    Interrupt m_interrupt = Interrupt::NONE;

    std::vector<Node> m_nodes;

    /// @brief Minimum depth at which each ground fluent atom has been seen true, or
    /// `kInfiniteDepth`. Grows on demand: in lifted mode the atom universe is only discovered as
    /// grounding proceeds, and an atom index beyond the end simply has not been seen yet.
    std::vector<uint32_t> m_best_depth;

    std::vector<uint32_t> m_order_scratch;

    bool m_root_solved = false;
    uint32_t m_goal_node = kInvalidNode;

    Statistics m_statistics;

    void ensure_feature_capacity(Index atom_index)
    {
        if (atom_index >= m_best_depth.size())
        {
            m_best_depth.resize(atom_index + 1, kInfiniteDepth);
        }
    }

    /// @brief The tightest plan-length bound currently known, from the options and from whatever a
    /// sibling worker has published since the last check.
    uint32_t current_incumbent_bound() const
    {
        auto bound = m_options.incumbent_bound;
        if (m_options.control)
        {
            bound = std::min(bound, m_options.control->get_incumbent_length());
        }
        return bound;
    }

    /// @brief Check the clock and the shared stop flag. Records why, so the caller can report it.
    bool should_interrupt()
    {
        if (m_stopwatch.has_finished())
        {
            m_interrupt = Interrupt::TIME;
            return true;
        }
        if (m_options.control && m_options.control->is_canceled())
        {
            m_interrupt = Interrupt::CANCELED;
            return true;
        }
        return false;
    }

    /// @brief Enumerate every applicable action of `node_index` into its action list.
    ///
    /// All of them, in one pass, with no resumable cursor. That is forced by the generators: a KPKC
    /// generator drives one coroutine over a single `DynamicAssignmentSets` that is rewritten per
    /// call, so at most one enumeration can be live at a time and a suspended cursor could not
    /// survive the rollout visiting another node. Materializing fully is also what makes "this node
    /// has no more children" exact, which SOLVED propagation needs.
    ///
    /// @return false if a budget expired mid-enumeration. The node is then left unmaterialized --
    /// a partial action list must never be mistaken for a complete one -- and the search stops.
    bool materialize_actions(uint32_t node_index, const State& state)
    {
        auto actions = GroundActionList {};
        auto yields = uint32_t(0);

        for (const auto& action : m_generator.create_applicable_action_generator(state))
        {
            actions.push_back(action);

            if (((++yields & kActionYieldCheckMask) == 0) && should_interrupt())
            {
                return false;
            }
        }

        auto& node = m_nodes[node_index];
        node.children.assign(actions.size(), kInvalidNode);
        node.actions = std::move(actions);
        node.actions_materialized = true;
        ++m_statistics.num_expanded_nodes;

        if (m_options.control)
        {
            m_options.control->add_expansions(1);
        }

        return true;
    }

    bool all_children_solved(uint32_t node_index) const
    {
        const auto& node = m_nodes[node_index];
        if (!node.actions_materialized)
        {
            return false;
        }
        for (const auto child : node.children)
        {
            if ((child == kInvalidNode) || (m_nodes[child].label != Label::SOLVED))
            {
                return false;
            }
        }
        return true;
    }

    /// @brief Label `node_index` SOLVED and propagate toward the root for as long as the parent's
    /// children are now all SOLVED. Labels are permanent, per the paper.
    void mark_solved(uint32_t node_index)
    {
        m_nodes[node_index].label = Label::SOLVED;

        auto current = m_nodes[node_index].parent;
        while ((current != kInvalidNode) && all_children_solved(current))
        {
            m_nodes[current].label = Label::SOLVED;
            ++m_statistics.num_solved_propagations;
            current = m_nodes[current].parent;
        }

        m_root_solved = (m_nodes.front().label == Label::SOLVED);
    }

    /// @brief Register a *new* node's features. Novel iff it lowers the minimum depth of at least
    /// one feature; every feature it lowers is updated, not just the first.
    bool register_new_node_features(const State& state, uint32_t depth)
    {
        auto improved = false;
        for (const auto atom_index : state.get_atoms<FluentTag>())
        {
            ensure_feature_capacity(atom_index);
            if (depth < m_best_depth[atom_index])
            {
                m_best_depth[atom_index] = depth;
                improved = true;
                ++m_statistics.num_feature_depth_improvements;
            }
        }
        return improved;
    }

    /// @brief Whether an *existing* node is still the best-known witness for some feature.
    ///
    /// For a node's own features `m_best_depth[f] <= depth` always holds once it has been
    /// registered, so this is really "some feature is still at exactly this depth" -- a feature can
    /// only be taken away by a shallower node found later on another branch.
    bool is_still_novel(const State& state, uint32_t depth)
    {
        for (const auto atom_index : state.get_atoms<FluentTag>())
        {
            ensure_feature_capacity(atom_index);
            if (depth <= m_best_depth[atom_index])
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Pick the first action in ranked order whose child is not yet SOLVED.
    /// @return the position in the node's action list, or `kInvalidNode` if every child is SOLVED.
    uint32_t select_action_position(uint32_t node_index, const State& state)
    {
        m_ordering->rank(state, m_nodes[node_index].actions, m_order_scratch);

        if (m_order_scratch.size() != m_nodes[node_index].actions.size())
        {
            throw std::runtime_error("rollout_iw: an action ordering strategy returned an order of the wrong size. Ordering is guidance, "
                                     "not pruning: every applicable action must stay in the permutation.");
        }

        for (const auto position : m_order_scratch)
        {
            const auto child = m_nodes[node_index].children.at(position);
            if ((child == kInvalidNode) || (m_nodes[child].label != Label::SOLVED))
            {
                return position;
            }
        }
        return kInvalidNode;
    }

    uint32_t add_node(PackedState packed_state, ContinuousCost metric_value, uint32_t parent, uint32_t incoming_action_pos, uint32_t depth)
    {
        const auto index = static_cast<uint32_t>(m_nodes.size());
        auto node = Node {};
        node.packed_state = packed_state;
        node.metric_value = metric_value;
        node.parent = parent;
        node.incoming_action_pos = incoming_action_pos;
        node.depth = depth;
        m_nodes.push_back(std::move(node));
        m_statistics.max_rollout_depth = std::max(m_statistics.max_rollout_depth, depth);
        return index;
    }

    /// @brief One rollout: descend from the root until a case ends it, a goal is found, or a budget
    /// expires.
    /// @return false if the whole search must stop.
    bool run_rollout()
    {
        auto current_index = uint32_t(0);
        auto current_state = m_state_repository.get_state(*m_nodes.front().packed_state);

        for (;;)
        {
            if (should_interrupt())
            {
                return false;
            }

            if (!m_nodes[current_index].actions_materialized && !materialize_actions(current_index, current_state))
            {
                return false;
            }

            if (m_nodes[current_index].actions.empty())
            {
                ++m_statistics.num_dead_ends;
                mark_solved(current_index);
                return true;
            }

            const auto position = select_action_position(current_index, current_state);
            if (position == kInvalidNode)
            {
                mark_solved(current_index);
                return true;
            }

            const auto existing_child = m_nodes[current_index].children[position];
            if (existing_child != kInvalidNode)
            {
                auto child_state = m_state_repository.get_state(*m_nodes[existing_child].packed_state);

                if (is_still_novel(child_state, m_nodes[existing_child].depth))
                {
                    ++m_statistics.num_case_4;
                    current_index = existing_child;
                    current_state = std::move(child_state);
                    continue;
                }

                ++m_statistics.num_case_3;
                mark_solved(existing_child);
                return true;
            }

            /* Untried action: generate the successor. */
            const auto action = m_nodes[current_index].actions[position];
            auto [successor_state, successor_metric_value] =
                m_state_repository.get_or_create_successor_state(current_state, action, m_nodes[current_index].metric_value);
            ++m_statistics.num_generated_states;

            const auto child_depth = m_nodes[current_index].depth + 1;
            const auto child_index = add_node(successor_state.get_packed_state(), successor_metric_value, current_index, position, child_depth);
            m_nodes[current_index].children[position] = child_index;

            /* The goal is tested before any bound reasoning, so a goal sitting exactly on the
               incumbent or depth boundary is still found rather than pruned away. */
            if (m_goal_strategy->test_dynamic_goal(successor_state))
            {
                m_goal_node = child_index;
                return false;
            }

            /* Checked here, right after the goal test, so that it also covers the paths below that
               prune the child and return: those still generated a state, and a search whose nodes
               are nearly all pruned would otherwise run past `max_num_states` indefinitely. The
               goal test stays ahead of it so a budget boundary never hides a solution. */
            if (m_statistics.num_generated_states >= m_options.max_num_states)
            {
                m_interrupt = Interrupt::STATES;
                return false;
            }

            if (child_depth >= m_options.max_depth)
            {
                ++m_statistics.num_depth_bound_prunings;
                mark_solved(child_index);
                return true;
            }

            /* A non-goal node at depth d needs at least one more action, so any plan through it has
               length >= d + 1. It cannot beat a known plan of length `bound` once d + 1 >= bound. */
            const auto bound = current_incumbent_bound();
            if ((bound != SearchControl::NO_INCUMBENT) && (child_depth + 1 >= bound))
            {
                ++m_statistics.num_incumbent_bound_prunings;
                mark_solved(child_index);
                return true;
            }

            if (register_new_node_features(successor_state, child_depth))
            {
                ++m_statistics.num_case_1;
                current_index = child_index;
                current_state = std::move(successor_state);
                continue;
            }

            ++m_statistics.num_case_2;
            mark_solved(child_index);
            return true;
        }
    }

    /// @brief Walk the tree from `m_goal_node` to the root, collecting the schema/binding steps and
    /// the ground actions in forward order.
    void extract_plan(Result& ref_result, const State& start_state, ContinuousCost start_metric_value) const
    {
        auto node_path = std::vector<uint32_t> {};
        for (auto current = m_goal_node; current != kInvalidNode; current = m_nodes[current].parent)
        {
            node_path.push_back(current);
        }
        std::reverse(node_path.begin(), node_path.end());

        auto actions = GroundActionList {};
        auto states = StateList { start_state };

        for (size_t i = 1; i < node_path.size(); ++i)
        {
            const auto& node = m_nodes[node_path[i]];
            const auto action = m_nodes[node.parent].actions.at(node.incoming_action_pos);

            actions.push_back(action);
            ref_result.plan_steps.push_back(PlanStep { action->get_action(), action->get_objects() });
            states.push_back(m_state_repository.get_state(*node.packed_state));
        }

        ref_result.plan_length = static_cast<uint32_t>(actions.size());

        const auto plan_cost = node_path.size() > 1 ? m_nodes[m_goal_node].metric_value : start_metric_value;
        ref_result.search_result.plan = Plan(m_context, std::move(states), std::move(actions), plan_cost);
        ref_result.search_result.goal_state = m_state_repository.get_state(*m_nodes[m_goal_node].packed_state);
        ref_result.search_result.status = SearchStatus::SOLVED;
    }

public:
    RolloutIWSearch(const SearchContext& context, const Options& options) :
        m_context(context),
        m_options(options),
        m_state_repository(*context->get_state_repository()),
        m_generator(*context->get_applicable_action_generator()),
        m_goal_strategy(options.goal_strategy ? options.goal_strategy : ProblemGoalStrategyImpl::create(context->get_problem(), options.goal_condition)),
        m_ordering(options.action_ordering ?
                       options.action_ordering :
                       create_action_ordering_strategy(options.action_ordering_configuration.value_or(
                                                           ActionOrderingConfiguration(ActionOrderingKind::IN_ORDER, options.seed)),
                                                       context->get_problem(),
                                                       options.goal_condition)),
        m_stopwatch(options.max_time_in_ms)
    {
    }

    Result run()
    {
        auto result = Result {};

        if (!m_goal_strategy->test_static_goal())
        {
            result.search_result.status = SearchStatus::UNSOLVABLE;
            result.stop_reason = "static goal cannot hold";
            return result;
        }

        const auto [start_state, start_metric_value] =
            m_options.start_state ? std::make_pair(m_options.start_state.value(), compute_state_metric_value(m_options.start_state.value())) :
                                    m_state_repository.get_or_create_initial_state();

        m_stopwatch.start();

        add_node(start_state.get_packed_state(), start_metric_value, kInvalidNode, kInvalidNode, 0);

        /* Root features sit at depth 0. Nothing can improve on that, so re-registering them at the
           start of each rollout -- as the reference algorithm is usually written -- is a no-op. */
        register_new_node_features(start_state, 0);

        if (m_goal_strategy->test_dynamic_goal(start_state))
        {
            m_goal_node = 0;
            extract_plan(result, start_state, start_metric_value);
            result.statistics = m_statistics;
            result.statistics.num_tree_nodes = m_nodes.size();
            return result;
        }

        while (!m_root_solved && (m_goal_node == kInvalidNode))
        {
            if (m_statistics.num_rollouts >= m_options.max_rollouts)
            {
                m_interrupt = Interrupt::ROLLOUTS;
                break;
            }

            ++m_statistics.num_rollouts;

            if (!run_rollout())
            {
                break;
            }
        }

        result.root_solved = m_root_solved;

        if (m_goal_node != kInvalidNode)
        {
            extract_plan(result, start_state, start_metric_value);
        }
        else
        {
            switch (m_interrupt)
            {
                case Interrupt::TIME:
                    result.search_result.status = SearchStatus::OUT_OF_TIME;
                    result.stop_reason = "time budget expired";
                    break;
                case Interrupt::CANCELED:
                    result.search_result.status = SearchStatus::CANCELED;
                    result.stop_reason = "canceled by shared search control";
                    break;
                case Interrupt::STATES:
                    result.search_result.status = SearchStatus::OUT_OF_STATES;
                    result.stop_reason = "state budget expired";
                    break;
                case Interrupt::ROLLOUTS:
                    result.search_result.status = SearchStatus::FAILED;
                    result.stop_reason = "rollout budget expired";
                    break;
                case Interrupt::NONE:
                    result.search_result.status = SearchStatus::EXHAUSTED;
                    result.stop_reason = (m_statistics.num_depth_bound_prunings + m_statistics.num_incumbent_bound_prunings > 0) ?
                                             "width-1 space exhausted under the depth/incumbent bounds" :
                                             "width-1 space exhausted";
                    break;
            }
        }

        result.statistics = m_statistics;
        result.statistics.num_tree_nodes = m_nodes.size();
        return result;
    }
};

}

Result find_solution(const SearchContext& context, const Options& options)
{
    if (!context)
    {
        throw std::runtime_error("rollout_iw::find_solution: context must not be null.");
    }
    if (options.start_state && (&options.start_state->get_problem() != context->get_problem().get()))
    {
        throw std::runtime_error("rollout_iw::find_solution: the given start state belongs to a different problem than the search context. "
                                 "State indices are per-problem; re-create the state through this context's state repository.");
    }
    /* Matching problems is not enough. A repository created with `PrivateInterningTables` has its
       own valla tables, so a state from a sibling repository of the *same* problem carries indices
       that decode to different atoms here -- silently, with no error anywhere. */
    if (options.start_state && (options.start_state->get_state_repository() != context->get_state_repository()))
    {
        throw std::runtime_error("rollout_iw::find_solution: the given start state belongs to a different state repository than the search "
                                 "context. Interning tables are per-repository; re-create the state through this context's state repository.");
    }

    return RolloutIWSearch(context, options).run();
}

}
