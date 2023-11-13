#include "goal_matcher.hpp"
#include "lifted_schema_successor_generator.hpp"

#include <algorithm>
#include <vector>

namespace planners
{
    bool GoalMatcher::is_ground(const formalism::AtomList& goal)
    {
        for (const auto& atom : goal)
        {
            for (const auto& parameter : atom->arguments)
            {
                if (parameter->is_free_variable())
                {
                    return false;
                }
            }
        }

        return true;
    }

    GoalMatcher::GoalMatcher(const planners::StateSpace& state_space) : state_space_(state_space), state_distances_()
    {
        for (const auto& state : state_space_->get_states())
        {
            state_distances_.emplace_back(state, state_space_->get_distance_to_goal_state(state));
        }

        const auto distance_ascending = [](const std::pair<formalism::State, int32_t>& lhs, const std::pair<formalism::State, int32_t>& rhs)
        { return lhs.second < rhs.second; };

        std::sort(state_distances_.begin(), state_distances_.end(), distance_ascending);
    }

    std::pair<formalism::State, int32_t> GoalMatcher::best_match(const formalism::AtomList& goal)
    {
        if (is_ground(goal))
        {
            const auto goal_ranks = state_space_->problem->to_ranks(goal);

            for (const auto& [state, distance] : state_distances_)
            {
                if (formalism::subset_of_state(goal_ranks, state))
                {
                    return std::make_pair(state, distance);
                }
            }

            return std::make_pair(nullptr, -1);
        }
        else
        {
            std::equal_to<formalism::Object> equal_to;
            formalism::ParameterList parameters;
            formalism::LiteralList precondition;
            formalism::LiteralList effect;

            for (const auto& atom : goal)
            {
                for (const auto& term : atom->arguments)
                {
                    if (term->is_free_variable())
                    {
                        bool new_term = true;

                        for (const auto& parameter : parameters)
                        {
                            if (equal_to(parameter, term))
                            {
                                new_term = false;
                                break;
                            }
                        }

                        if (new_term)
                        {
                            parameters.emplace_back(term);
                        }
                    }
                }

                precondition.emplace_back(formalism::create_literal(atom, false));
            }

            const auto unit_cost = formalism::create_unit_cost_function(state_space_->domain);
            const auto action_schema = formalism::create_action_schema("dummy", parameters, precondition, effect, {}, unit_cost);
            planners::LiftedSchemaSuccessorGenerator successor_generator(action_schema, state_space_->problem);

            for (const auto& [state, distance] : state_distances_)
            {
                const auto matches = successor_generator.get_applicable_actions(state);

                if (matches.size() > 0)
                {
                    return std::make_pair(state, distance);
                }
            }

            return std::make_pair(nullptr, -1);
        }
    }
}  // namespace planners