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

#include "mimir/graphs/object_graph.hpp"

#include "mimir/common/itertools.hpp"
#include "mimir/formalism/repositories.hpp"

#include <fmt/ranges.h>
#include <range/v3/view/filter.hpp>

namespace mimir
{

/**
 * ObjectGraph
 */

static std::unordered_map<Object, VertexIndex> add_objects_graph_structures(const ProblemColorFunction& color_function,
                                                                            Problem problem,
                                                                            Index state_index,
                                                                            const ObjectGraphPruningStrategy& pruning_strategy,
                                                                            StaticVertexColoredDigraph& out_digraph)
{
    std::unordered_map<Object, VertexIndex> object_to_vertex_index;

    for (const auto& object : problem->get_objects())
    {
        if (!pruning_strategy.prune(state_index, object))
        {
            const auto vertex_color = color_function.get_color(object);
            const auto vertex_index = out_digraph.add_vertex(vertex_color);
            object_to_vertex_index.emplace(object, vertex_index);
        }
    }
    return object_to_vertex_index;
}

template<PredicateTag P>
static void add_ground_atom_graph_structures(const ProblemColorFunction& color_function,
                                             const std::unordered_map<Object, VertexIndex>& object_to_vertex_index,
                                             GroundAtom<P> atom,
                                             StaticVertexColoredDigraph& out_digraph)
{
    for (size_t pos = 0; pos < atom->get_arity(); ++pos)
    {
        const auto vertex_color = color_function.get_color(atom, pos);
        const auto vertex_index = out_digraph.add_vertex(vertex_color);
        out_digraph.add_undirected_edge(vertex_index, object_to_vertex_index.at(atom->get_objects().at(pos)));
        if (pos > 0)
        {
            out_digraph.add_undirected_edge(vertex_index - 1, vertex_index);
        }
    }
}

template<PredicateTag P>
static void add_ground_literal_graph_structures(const ProblemColorFunction& color_function,
                                                const std::unordered_map<Object, VertexIndex>& object_to_vertex_index,
                                                bool mark_true_goal_literals,
                                                State state,
                                                GroundLiteral<P> literal,
                                                StaticVertexColoredDigraph& out_digraph)
{
}

StaticVertexColoredDigraph create_object_graph(const ProblemColorFunction& color_function,
                                               const PDDLRepositories& pddl_repositories,
                                               Problem problem,
                                               State state,
                                               Index state_index,
                                               bool mark_true_goal_literals,
                                               const ObjectGraphPruningStrategy& pruning_strategy)
{
    auto vertex_colored_digraph = StaticVertexColoredDigraph();

    const auto object_to_vertex_index = add_objects_graph_structures(color_function, problem, state_index, pruning_strategy, vertex_colored_digraph);

    // add ground atoms graph structures
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            const auto& ground_atom_indices = std::invoke(
                [&]
                {
                    if constexpr (std::is_same_v<Tag, Static>)
                    {
                        return pddl_repositories.get_ground_atoms_from_indices<Tag>(problem->get_static_initial_positive_atoms_bitset());
                    }
                    else
                    {
                        return pddl_repositories.get_ground_atoms_from_indices<Tag>(state->get_atoms<Tag>());
                    }
                });
            for (const auto& atom : ground_atom_indices | std::views::filter([&](const auto& atom) { return not pruning_strategy.prune(state_index, atom); }))
            {
                for (size_t pos : std::views::iota(0u, atom->get_arity()))
                {
                    const auto vertex_color = color_function.get_color(atom, pos);
                    const auto vertex_index = vertex_colored_digraph.add_vertex(vertex_color);
                    vertex_colored_digraph.add_undirected_edge(vertex_index, object_to_vertex_index.at(atom->get_objects().at(pos)));
                    if (pos > 0)
                    {
                        vertex_colored_digraph.add_undirected_edge(vertex_index - 1, vertex_index);
                    }
                }
            }
        });

    // add ground goal literal graph structures
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            for (const auto& literal :
                 problem->get_goal_condition<Tag>() | std::views::filter([&](const auto& literal) { return not pruning_strategy.prune(state_index, literal); }))
            {
                for (size_t pos : std::views::iota(0u, literal->get_atom()->get_arity()))
                {
                    const auto vertex_color = color_function.get_color(state, literal, pos, mark_true_goal_literals);
                    const auto vertex_index = vertex_colored_digraph.add_vertex(vertex_color);
                    vertex_colored_digraph.add_undirected_edge(vertex_index, object_to_vertex_index.at(literal->get_atom()->get_objects().at(pos)));
                    if (pos > 0)
                    {
                        vertex_colored_digraph.add_undirected_edge(vertex_index - 1, vertex_index);
                    }
                }
            }
        });

    return vertex_colored_digraph;
}
}
