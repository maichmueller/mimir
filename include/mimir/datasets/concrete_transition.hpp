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

#ifndef MIMIR_DATASETS_CONCRETE_TRANSITION_HPP_
#define MIMIR_DATASETS_CONCRETE_TRANSITION_HPP_

#include "mimir/datasets/declarations.hpp"
#include "mimir/graphs/graph_edges.hpp"
#include "mimir/search/action.hpp"
#include "mimir/search/state.hpp"

#include <span>
#include <vector>

namespace mimir
{

struct ConcreteTransitionTag
{
};

using ConcreteTransition = Edge<ConcreteTransitionTag, GroundAction>;
using ConcreteTransitionList = std::vector<ConcreteTransition>;

inline GroundAction get_creating_action(const ConcreteTransition& concrete_transition) { return concrete_transition.get_property<0>(); }

inline TransitionCost get_cost(const ConcreteTransition& concrete_transition) { return get_creating_action(concrete_transition).get_cost(); }

}

#endif
