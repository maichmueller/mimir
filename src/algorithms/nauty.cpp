/*
 * Copyright (C) 2023 Simon Stahlberg
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

#include "mimir/algorithms/nauty.hpp"

#include "nauty_dense_impl.hpp"
#include "nauty_sparse_impl.hpp"

#include <cassert>
#include <stdexcept>

namespace nauty_wrapper
{

/* Graph */
DenseGraph::DenseGraph() : m_impl(std::make_unique<DenseGraphImpl>(0)) {}

DenseGraph::DenseGraph(size_t num_vertices) : m_impl(std::make_unique<DenseGraphImpl>(num_vertices)) {}

DenseGraph::DenseGraph(const DenseGraph& other) : m_impl(std::make_unique<DenseGraphImpl>(*other.m_impl)) {}

DenseGraph& DenseGraph::operator=(const DenseGraph& other)
{
    if (this != &other)
    {
        m_impl = std::make_unique<DenseGraphImpl>(*other.m_impl);
    }
    return *this;
}

DenseGraph::DenseGraph(DenseGraph&& other) noexcept : m_impl(std::move(other.m_impl)) {}

DenseGraph& DenseGraph::operator=(DenseGraph&& other) noexcept
{
    if (this != &other)
    {
        std::swap(m_impl, other.m_impl);
    }
    return *this;
}

DenseGraph::~DenseGraph() = default;

void DenseGraph::add_edge(size_t source, size_t target) { m_impl->add_edge(source, target); }

std::string DenseGraph::compute_certificate(const mimir::Partitioning& partitioning) const { return m_impl->compute_certificate(partitioning); }

void DenseGraph::reset(size_t num_vertices) { m_impl->reset(num_vertices); }

bool DenseGraph::is_directed() const { return m_impl->is_directed(); }

/* DenseGraphFactory */

DenseGraph& DenseGraphFactory::create_from_digraph(const mimir::graphs::Digraph& digraph)
{
    m_graph.reset(digraph.get_num_vertices());

    for (const auto& edge : digraph.get_edges())
    {
        m_graph.add_edge(edge.get_source(), edge.get_target());
    }

    return m_graph;
}

/* SparseGraph*/

SparseGraph::SparseGraph() : m_impl(std::make_unique<SparseGraphImpl>(0)) {}

SparseGraph::SparseGraph(size_t num_vertices) : m_impl(std::make_unique<SparseGraphImpl>(num_vertices)) {}

SparseGraph::SparseGraph(const SparseGraph& other) : m_impl(std::make_unique<SparseGraphImpl>(*other.m_impl)) {}

SparseGraph& SparseGraph::operator=(const SparseGraph& other)
{
    if (this != &other)
    {
        m_impl = std::make_unique<SparseGraphImpl>(*other.m_impl);
    }
    return *this;
}

SparseGraph::SparseGraph(SparseGraph&& other) noexcept : m_impl(std::move(other.m_impl)) {}

SparseGraph& SparseGraph::operator=(SparseGraph&& other) noexcept
{
    if (this != &other)
    {
        std::swap(m_impl, other.m_impl);
    }
    return *this;
}

SparseGraph::~SparseGraph() = default;

void SparseGraph::add_edge(size_t source, size_t target) { m_impl->add_edge(source, target); }

std::string SparseGraph::compute_certificate(const mimir::Partitioning& partitioning) { return m_impl->compute_certificate(partitioning); }

void SparseGraph::reset(size_t num_vertices) { m_impl->reset(num_vertices); }

bool SparseGraph::is_directed() const { return m_impl->is_directed(); }

/* SparseGraphFactory */

SparseGraph& SparseGraphFactory::create_from_digraph(const mimir::graphs::Digraph& digraph)
{
    m_graph.reset(digraph.get_num_vertices());

    for (const auto& edge : digraph.get_edges())
    {
        m_graph.add_edge(edge.get_source(), edge.get_target());
    }

    return m_graph;
}

}