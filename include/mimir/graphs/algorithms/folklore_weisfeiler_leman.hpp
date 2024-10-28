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

#ifndef MIMIR_GRAPHS_ALGORITHMS_FOLKLORE_WEISFEILER_LEMAN_HPP_
#define MIMIR_GRAPHS_ALGORITHMS_FOLKLORE_WEISFEILER_LEMAN_HPP_

#include "mimir/algorithms/nauty.hpp"
#include "mimir/common/equal_to.hpp"
#include "mimir/common/hash.hpp"
#include "mimir/common/printers.hpp"
#include "mimir/common/types.hpp"
#include "mimir/graphs/algorithms/color_refinement.hpp"
#include "mimir/graphs/digraph_vertex_colored.hpp"
#include "mimir/graphs/graph_interface.hpp"
#include "mimir/graphs/graph_traversal_interface.hpp"
#include "mimir/graphs/graph_vertices.hpp"

#include <cassert>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace mimir::kfwl
{
using mimir::operator<<;

/// @brief `Certificate` encapsulates the final tuple colorings and the decoding tables.
/// @tparam K is the dimensionality.
template<size_t K>
class Certificate
{
public:
    /* Compression of new color to map (C(bar{v}), {{(c_1^1, ...,c_k^1), ..., (c_1^r, ...,c_k^r)}}) to an integer color for bar{v} in V^k */
    using ConfigurationCompressionFunction =
        std::unordered_map<std::pair<Color, std::vector<ColorArray<K>>>, Color, Hash<std::pair<Color, std::vector<ColorArray<K>>>>>;
    using CanonicalConfigurationCompressionFunction = std::map<std::pair<Color, std::vector<ColorArray<K>>>, Color>;

    using CanonicalColoring = std::set<Color>;

    Certificate(ConfigurationCompressionFunction f, ColorList hash_to_color) :
        m_hash_to_color(std::move(hash_to_color)),
        m_f(f.begin(), f.end()),
        m_coloring_coloring(m_hash_to_color.begin(), m_hash_to_color.end())
    {
    }

    const CanonicalConfigurationCompressionFunction& get_canonical_configuration_compression_function() const { return m_f; }
    const CanonicalColoring& get_canonical_coloring() const { return m_coloring_coloring; }

private:
    ColorList m_hash_to_color;

    CanonicalConfigurationCompressionFunction m_f;
    CanonicalColoring m_coloring_coloring;
};

template<size_t K>
class IsomorphismTypeFunction
{
public:
    /* Compression of isomorphic types. */
    using IsomorphicTypeCompressionFunction = std::unordered_map<nauty_wrapper::Certificate, Color>;

    IsomorphicTypeCompressionFunction& get_isomorphic_type_compression_function() { return m_f1; }

private:
    IsomorphicTypeCompressionFunction m_f1;
};

template<size_t K>
bool operator==(const Certificate<K>& lhs, const Certificate<K>& rhs)
{
    if (&lhs != &rhs)
    {
        return (lhs.get_canonical_coloring() == rhs.get_canonical_coloring())
               && (lhs.get_canonical_configuration_compression_function() == rhs.get_canonical_configuration_compression_function());
    }
    return true;
}

template<size_t K>
std::ostream& operator<<(std::ostream& out, const Certificate<K>& element)
{
    out << "Certificate" << K << "FWL("
        << "canonical_coloring=" << element.get_canonical_coloring() << ", "
        << "canonical_configuration_compression_function=" << element.get_canonical_configuration_compression_function() << ")";
    return out;
}

/// @brief Compute the perfect hash of the given k-tuple.
/// @tparam K is the dimensionality.
/// @param tuple is the k-tuple.
/// @param num_vertices is the number of vertices in the graph.
/// @return the perfect hash of the k-tuple.
template<size_t K>
size_t tuple_to_hash(const IndexArray<K>& tuple, size_t num_vertices)
{
    size_t hash = 0;
    for (size_t i = 0; i < K; ++i)
    {
        hash = hash * num_vertices + tuple[i];
    }
    return hash;
}

/// @brief Compute the k-tuple of the perfect hash.
/// This operation takes O(k) time.
/// @tparam K is the dimensionality.
/// @param hash is the perfect hash.
/// @param num_vertices is the number of vertices in the graph.
/// @return the k-tuple of the perfect hash.
template<size_t K>
IndexArray<K> hash_to_tuple(size_t hash, size_t num_vertices)
{
    auto result = IndexArray<K>();
    for (int64_t i = K - 1; i >= 0; --i)
    {
        result[i] = hash % num_vertices;
        hash /= num_vertices;  // Reduces the hash for the next position
    }
    return result;
}

template<size_t K, typename G>
requires IsVertexListGraph<G> && IsIncidenceGraph<G> && IsVertexColoredGraph<G>  //
    ColorList compute_ordered_isomorphism_types(const G& graph, IsomorphismTypeFunction<K>& iso_type_function)
{
    const auto num_vertices = graph.get_num_vertices();
    const auto num_hashes = std::pow(num_vertices, K);

    /* Temporary bookkeeping to support dynamic graphs. */
    auto vertex_to_v = IndexMap<Index>();
    auto v_to_vertex = IndexMap<Index>();
    for (const auto& vertex : graph.get_vertex_indices())
    {
        const auto v = Index(vertex_to_v.size());
        vertex_to_v.emplace(vertex, v);
        v_to_vertex.emplace(v, vertex);
    }

    // Create adj matrix for fast creation of subgraph induced by k-tuple.
    auto adj_matrix = std::vector<std::vector<bool>>(num_vertices, std::vector<bool>(num_vertices, false));
    for (const auto& vertex1 : graph.get_vertex_indices())
    {
        for (const auto& vertex2 : graph.template get_adjacent_vertex_indices<ForwardTraversal>(vertex1))
        {
            adj_matrix.at(vertex_to_v.at(vertex1)).at(vertex_to_v.at(vertex2)) = true;
        }
    }

    auto hash_to_color = ColorList(num_hashes);

    auto subgraph = nauty_wrapper::SparseGraph(K);
    auto subgraph_coloring = ColorList();
    auto iso_types = std::vector<std::pair<nauty_wrapper::Certificate, Index>>();

    // Subroutine to compute (ordered) isomorphic types of all k-tuples of vertices.
    auto v_to_i = IndexMap<Index>();
    for (size_t hash = 0; hash < num_hashes; ++hash)
    {
        // Compress indexing of subgraph.
        v_to_i.clear();
        auto t = hash_to_tuple<K>(hash, num_vertices);
        for (size_t i = 0; i < K; ++i)
        {
            v_to_i.emplace(t[i], v_to_i.size());
        }

        // Initialize empty subgraph and coloring
        const auto subgraph_num_vertices = v_to_i.size();
        subgraph.clear(subgraph_num_vertices);
        subgraph_coloring.resize(subgraph_num_vertices);

        // Instantiate vertex-colored subgraph.
        for (const auto [v1, i1] : v_to_i)
        {
            for (const auto [v2, i2] : v_to_i)
            {
                if (adj_matrix[v1][v2])
                {
                    subgraph.add_edge(i1, i2);
                }
            }
            subgraph_coloring[i1] = get_color(graph.get_vertex(v_to_vertex.at(v1)));
        }
        subgraph.add_vertex_coloring(subgraph_coloring);

        // Compute certificate for k-tuple hash.
        iso_types.emplace_back(subgraph.compute_certificate(), hash);
    }

    // Create ordered iso-types.
    std::sort(iso_types.begin(), iso_types.end());
    for (const auto& [certificate, hash] : iso_types)
    {
        auto num_iso_types = iso_type_function.get_isomorphic_type_compression_function().size();
        auto result = iso_type_function.get_isomorphic_type_compression_function().insert(std::make_pair(certificate, num_iso_types));
        const auto color = result.first->second;
        hash_to_color.at(hash) = color;
    }

    return hash_to_color;
}

/// @brief `compute_certificate` implements the k-dimensional Folklore Weisfeiler-Leman algorithm.
/// Source: https://arxiv.org/pdf/1907.09582
/// @tparam G is the vertex-colored graph.
/// @tparam K is the dimensionality.
/// @return the `Certicate`
template<size_t K, typename G>
requires IsVertexListGraph<G> && IsIncidenceGraph<G> && IsVertexColoredGraph<G>  //
    Certificate<K> compute_certificate(const G& graph, IsomorphismTypeFunction<K>& iso_type_function)
{
    // Toggle verbosity
    const bool debug = false;

    /* Fetch some data. */
    const auto num_vertices = graph.get_num_vertices();
    const auto num_hashes = std::pow(num_vertices, K);

    /* Compute max color used in graph. */
    auto max_color = Color();
    for (const auto& vertex : graph.get_vertex_indices())
    {
        max_color = std::max(max_color, get_color(graph.get_vertex(vertex)));
    }

    /* Compute isomorphism types. */
    auto hash_to_color = compute_ordered_isomorphism_types<K>(graph, iso_type_function);

    /* Refine colors of k-tuples. */
    auto f = typename Certificate<K>::ConfigurationCompressionFunction();
    auto M = std::vector<std::pair<Index, ColorArray<K>>>();
    auto M_replaced = std::vector<std::tuple<Color, std::vector<ColorArray<K>>, Index>>();
    // (line 3-18): subroutine to find stable coloring
    bool is_stable = false;
    do
    {
        // Clear data structures that are reused.
        M.clear();
        M_replaced.clear();

        {
            // (lines 4-14): Subroutine to fill multiset
            {
                for (size_t h = 0; h < num_hashes; ++h)
                {
                    const auto w = hash_to_tuple<K>(h, num_vertices);

                    for (size_t j = 0; j < K; ++j)
                    {
                        for (size_t u = 0; u < num_vertices; ++u)
                        {
                            // \vec{v} = \vec{w}[j,u]
                            auto v = w;
                            v[j] = u;
                            const auto v_hash = tuple_to_hash(v, num_vertices);

                            // C[\vec{v}[1,u]],...,C[\vec{v}[k,u]]
                            auto k_coloring = ColorArray<K>();
                            for (size_t i = 0; i < K; ++i)
                            {
                                // \vec{x} = \vec{v}[i,u]
                                auto x = v;
                                x[i] = u;
                                const auto x_hash = tuple_to_hash<K>(x, num_vertices);

                                k_coloring.at(i) = hash_to_color.at(x_hash);
                            }
                            M.emplace_back(v_hash, std::move(k_coloring));
                        }
                    }
                }
            }
        }

        // (line 15): Perform radix sort of M
        std::sort(M.begin(), M.end());

        if (debug)
            std::cout << "M: " << M << std::endl;

        // (line 16): Scan M and replace tuples (vec{v},c_1^1,...,c_k^1,...,vec{v},c_1^r,...,c_k^r) with single tuple
        // (C(vec{v}),(c_1^1,...,c_k^1),...,(c_1^r,...,c_k^r))
        color_refinement::replace_tuples(M, hash_to_color, M_replaced);

        // (line 17): Perform radix sort of M
        std::sort(M_replaced.begin(), M_replaced.end());

        if (debug)
            std::cout << "M_replaced: " << M_replaced << std::endl;

        // (line 18): Split color classes
        is_stable = color_refinement::split_color_classes(M_replaced, f, max_color, hash_to_color);

    } while (!is_stable);

    /* Report final neighborhood structures in the decoding table. */
    for (const auto& [old_color, signature, hash] : M_replaced)
    {
        f.emplace(std::make_pair(old_color, signature), old_color);
    }

    /* Return the certificate */
    return Certificate(std::move(f), std::move(hash_to_color));
}

    /* Bookkeeping to support dynamic graphs. */
    auto vertex_to_v = IndexMap<Index>();
    auto v_to_vertex = IndexMap<Index>();
    for (const auto& vertex : graph.get_vertex_indices())
    {
        auto v = Index(vertex_to_v.size());
        vertex_to_v.emplace(vertex, v);
        v_to_vertex.emplace(v, vertex);
    }

    /* Initialize result data structures. */
    auto tuple_to_color = ColorList();
    auto f = Certificate<K>::IsomorphicTypeCompressionFunction();
    auto g = Certificate<K>::TupleColorCompressionFunction();
    auto h = Certificate<K>::ConfigurationCompressionFunction();

    /* Compute isomorphism types. */
    // Create adj matrix for fast creation of subgraph induced by k-tuple.
    auto adj_matrix = std::vector<std::vector<bool>>(std::vector<bool>(false, num_vertices), num_vertices);
    for (const auto& vertex1 : graph.get_vertex_indices<ForwardTraversal>())
    {
        for (const auto& vertex2 : graph.get_adjacent_vertex_indices<ForwardTraversal>())
        {
            adj_matrix.at(vertex_to_v.at(vertex1)).at(vertex_to_v.at(vertex2)) = true;
        }
    }

    auto subgraph = nauty_wrapper::SparseGraph(K);
    auto subgraph_coloring = ColorList();
    auto iso_types = std::vector<std::pair<nauty_wrapper::Certificate, Index>>();

    for (size_t h = 0; h < std::pow(num_vertices, K), ++h)
    {
        auto t = hash_to_tuple<K>(h, num_vertices);
        for (const auto& v1 : h)
        {
            for (const auto& v2 : h)
            {
                if (adj_matrix.at(v1).at(v2))
                {
                    subgraph.add_edge(v1, v2);
                }
            }
            subgraph_coloring.push_back(get_color(graph.get_vertex(v_to_vertex.at(v1))));
        }

        iso_types.emplace(subgraph.compute_certificate(), h);

        subgraph.clear();
        subgraph_coloring.clear();
    }

    // Create ordered iso-types.
    std::sort(iso_types.begin(), iso_types.end());
    for (const auto& [certificate, h] : iso_types)
    {
        auto result = f.insert(certificate, f.size());
        tuple_to_color.push_back(result.first->second);
    }

    /* TODO: refine */

    /* TODO: return certificate */
}

}

template<size_t K>
struct std::hash<mimir::kfwl::Certificate<K>>
{
    size_t operator()(const mimir::kfwl::Certificate<K>& element) const
    {
        return mimir::hash_combine(element.get_canonical_coloring(), element.get_canonical_configuration_compression_function());
    }
};

#endif