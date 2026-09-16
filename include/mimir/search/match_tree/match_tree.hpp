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

#ifndef MIMIR_SEARCH_MATCH_TREE_MATCH_TREE_HPP_
#define MIMIR_SEARCH_MATCH_TREE_MATCH_TREE_HPP_

#include "mimir/algorithms/generator.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_axiom.hpp"
#include "mimir/search/match_tree/declarations.hpp"
#include "mimir/search/match_tree/node_splitters/interface.hpp"
#include "mimir/search/match_tree/nodes/interface.hpp"
#include "mimir/search/match_tree/options.hpp"
#include "mimir/search/match_tree/statistics.hpp"

namespace mimir::search::match_tree
{
/* MatchTree */
template<formalism::HasConjunctiveCondition E>
class MatchTreeImpl
{
private:
    std::vector<const E*> m_elements;  ///< ATTENTION: must remain persistent. Swapping elements is allowed.
    Options m_options;

    Node<E> m_root;
    Statistics m_statistics;

    MatchTreeImpl();

    MatchTreeImpl(const formalism::Repositories& pddl_repositories, std::vector<const E*> elements, const Options& options = Options());

public:
    static std::unique_ptr<MatchTreeImpl<E>>
    create(const formalism::Repositories& pddl_repositories, std::vector<const E*> elements, const Options& options = Options());

    // Uncopieable and unmoveable to prohibit invalidating spans on m_elements.
    MatchTreeImpl(const MatchTreeImpl& other) = delete;
    MatchTreeImpl& operator=(const MatchTreeImpl& other) = delete;
    MatchTreeImpl(MatchTreeImpl&& other) = delete;
    MatchTreeImpl& operator=(MatchTreeImpl&& other) = delete;

    void generate_applicable_elements_iteratively(const UnpackedStateImpl& state, std::vector<const E*>& out_applicable_elements);

    /// @brief Lazily yield the applicable elements in the same order as
    /// `generate_applicable_elements_iteratively`, expanding one tree node per resumption.
    ///
    /// A consumer that abandons the generator early pays only for the nodes visited up to that
    /// point, which is what makes an existence test affordable on states whose full applicable set
    /// runs into the millions. Each generator holds its own traversal stack for its lifetime, so
    /// nested and interleaved generators over the same tree are safe -- unlike
    /// `generate_applicable_elements_iteratively`, which shares one thread-local stack. The buffers
    /// come from a per-thread free list, so this costs no allocation in steady state.
    ///
    /// @param state must outlive the returned generator: the coroutine frame holds it by reference.
    mimir::generator<const E*> create_applicable_elements_generator(const UnpackedStateImpl& state);

    /// @brief Return whether at least one element is applicable in the state, stopping at the first
    /// one found rather than materializing the applicable set.
    bool has_applicable_element(const UnpackedStateImpl& state);

    const Statistics& get_statistics() const;
};

}

#endif
