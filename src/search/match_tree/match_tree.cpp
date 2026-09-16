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

#include "mimir/search/match_tree/match_tree.hpp"

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_axiom.hpp"
#include "mimir/search/match_tree/construction_helpers/node_creation.hpp"
#include "mimir/search/match_tree/declarations.hpp"
#include "mimir/search/match_tree/node_splitters/dynamic.hpp"
#include "mimir/search/match_tree/nodes/generator.hpp"
#include "mimir/search/match_tree/nodes/interface.hpp"

#include <memory>
#include <queue>

using namespace mimir::formalism;

namespace mimir::search::match_tree
{

/* MatchTree */

template<formalism::HasConjunctiveCondition E>
MatchTreeImpl<E>::MatchTreeImpl() : m_elements(), m_options(), m_root(create_root_generator_node(std::span<const E*>(m_elements.begin(), m_elements.end())))
{
    m_statistics.generator_distribution.push_back(0);
}

template<formalism::HasConjunctiveCondition E>
MatchTreeImpl<E>::MatchTreeImpl(const Repositories& pddl_repositories, std::vector<const E*> elements, const Options& options) :
    m_elements(std::move(elements)),
    m_options(options),
    m_root(create_root_generator_node(std::span<const E*>(m_elements.begin(), m_elements.end())))
{
    if (!m_elements.empty())
    {
        auto node_splitter = NodeSplitter<E> { nullptr };
        switch (m_options.split_strategy)
        {
            case SplitStrategyEnum::DYNAMIC:
            {
                node_splitter =
                    std::make_unique<DynamicNodeSplitter<E>>(pddl_repositories, m_options, std::span<const E*>(m_elements.begin(), m_elements.end()));
                break;
            }
            case SplitStrategyEnum::HYBRID:
            {
                throw std::runtime_error("Not implemented.");
            }
            case SplitStrategyEnum::STATIC:
            {
                throw std::runtime_error("Not implemented.");
            }
            default:
            {
                throw std::logic_error("MatchTree::create: Undefined SplitStrategyEnum type.");
            }
        }

        auto [root_, statistics_] = node_splitter->fit(m_elements);
        m_root = std::move(root_);
        m_statistics = std::move(statistics_);
    }
}

template<formalism::HasConjunctiveCondition E>
void MatchTreeImpl<E>::generate_applicable_elements_iteratively(const UnpackedStateImpl& state, std::vector<const E*>& out_applicable_elements)
{
    static thread_local auto s_evaluate_stack = std::vector<const INode<E>*> {};
    s_evaluate_stack.clear();
    out_applicable_elements.clear();

    s_evaluate_stack.push_back(m_root.get());

    while (!s_evaluate_stack.empty())
    {
        const auto node = s_evaluate_stack.back();

        s_evaluate_stack.pop_back();

        node->generate_applicable_actions(state, s_evaluate_stack, out_applicable_elements);
    }
}

namespace
{
/// @brief The two buffers one traversal of a match tree needs.
template<formalism::HasConjunctiveCondition E>
struct TraversalScratch
{
    std::vector<const INode<E>*> stack;
    std::vector<const E*> node_elements;
};

/// @brief A per-thread free list of traversal buffers, and whether it is still usable.
///
/// `generate_applicable_elements_iteratively` can share one thread-local stack because it returns
/// before anyone else runs. A *lazy* traversal cannot: it stays suspended across arbitrary caller
/// code, and a nested traversal would pull the stack out from under it. Giving every traversal its
/// own buffers fixes that but costs two allocations per state expansion, which is about 5% of a
/// grounded breadth-first search -- so they are recycled instead. Each traversal holds a buffer
/// pair exclusively for its lifetime, and a steady-state search allocates nothing here.
template<formalism::HasConjunctiveCondition E>
struct TraversalScratchPool
{
    std::vector<std::unique_ptr<TraversalScratch<E>>> free_list;
    bool accepting = true;

    /// The pool outlives every traversal on its own thread in every ordinary case, but a coroutine
    /// frame destroyed during thread teardown could otherwise try to return a buffer into a
    /// free list that is already being destroyed. After this runs, returns are dropped instead.
    ~TraversalScratchPool()
    {
        accepting = false;
        free_list.clear();
    }
};

/// Bounded so that one traversal over a pathologically large state does not leave its capacity
/// parked on the thread forever once the search has moved on.
constexpr size_t MAX_POOLED_TRAVERSAL_SCRATCH = 4;

template<formalism::HasConjunctiveCondition E>
TraversalScratchPool<E>& traversal_scratch_pool()
{
    static thread_local auto pool = TraversalScratchPool<E> {};
    return pool;
}

/// @brief Holds one traversal's buffers, returning them to the thread's pool on destruction.
template<formalism::HasConjunctiveCondition E>
class TraversalScratchHandle
{
public:
    TraversalScratchHandle()
    {
        auto& pool = traversal_scratch_pool<E>();

        if (pool.free_list.empty())
        {
            m_scratch = std::make_unique<TraversalScratch<E>>();
        }
        else
        {
            m_scratch = std::move(pool.free_list.back());
            pool.free_list.pop_back();
        }

        m_scratch->stack.clear();
        m_scratch->node_elements.clear();
    }

    ~TraversalScratchHandle()
    {
        auto& pool = traversal_scratch_pool<E>();

        if (m_scratch && pool.accepting && pool.free_list.size() < MAX_POOLED_TRAVERSAL_SCRATCH)
        {
            pool.free_list.push_back(std::move(m_scratch));
        }
    }

    TraversalScratchHandle(const TraversalScratchHandle&) = delete;
    TraversalScratchHandle& operator=(const TraversalScratchHandle&) = delete;
    TraversalScratchHandle(TraversalScratchHandle&&) = delete;
    TraversalScratchHandle& operator=(TraversalScratchHandle&&) = delete;

    TraversalScratch<E>& operator*() const { return *m_scratch; }

private:
    std::unique_ptr<TraversalScratch<E>> m_scratch;
};
}

template<formalism::HasConjunctiveCondition E>
mimir::generator<const E*> MatchTreeImpl<E>::create_applicable_elements_generator(const UnpackedStateImpl& state)
{
    auto scratch = TraversalScratchHandle<E> {};
    auto& evaluate_stack = (*scratch).stack;
    auto& node_elements = (*scratch).node_elements;

    evaluate_stack.push_back(m_root.get());

    while (!evaluate_stack.empty())
    {
        const auto node = evaluate_stack.back();

        evaluate_stack.pop_back();

        /* Nodes only append, so clearing per node is what keeps the buffer bounded by one node's
           output instead of the whole applicable set, while preserving the order of the eager
           traversal exactly. */
        node_elements.clear();
        node->generate_applicable_actions(state, evaluate_stack, node_elements);

        for (const auto& element : node_elements)
        {
            co_yield element;
        }
    }
}

template<formalism::HasConjunctiveCondition E>
bool MatchTreeImpl<E>::has_applicable_element(const UnpackedStateImpl& state)
{
    /* Deliberately not `create_applicable_elements_generator(state).begin() != end()`: the answer is
       one bit, and this way it costs no coroutine frame. */
    auto scratch = TraversalScratchHandle<E> {};
    auto& evaluate_stack = (*scratch).stack;
    auto& node_elements = (*scratch).node_elements;

    evaluate_stack.push_back(m_root.get());

    while (!evaluate_stack.empty())
    {
        const auto node = evaluate_stack.back();

        evaluate_stack.pop_back();

        node_elements.clear();
        node->generate_applicable_actions(state, evaluate_stack, node_elements);

        if (!node_elements.empty())
        {
            return true;
        }
    }

    return false;
}

template<formalism::HasConjunctiveCondition E>
const Statistics& MatchTreeImpl<E>::get_statistics() const
{
    return m_statistics;
}

template<formalism::HasConjunctiveCondition E>
std::unique_ptr<MatchTreeImpl<E>> MatchTreeImpl<E>::create(const Repositories& pddl_repositories, std::vector<const E*> elements, const Options& options)
{
    return std::unique_ptr<MatchTreeImpl<E>>(new MatchTreeImpl<E>(pddl_repositories, std::move(elements), options));
}

template class MatchTreeImpl<GroundActionImpl>;
template class MatchTreeImpl<GroundAxiomImpl>;
}
