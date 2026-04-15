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

#ifndef MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_INCREMENTAL_IW1_HPP_
#define MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_INCREMENTAL_IW1_HPP_

#include "internal.hpp"

#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/statistics.hpp"
#include "mimir/search/search_context.hpp"

#include <chrono>
#include <span>
#include <vector>

namespace mimir::search::brfs
{

class IW1IncrementalActionDiscoveryController
{
public:
    struct PreconditionTrigger
    {
        formalism::Action action_schema = nullptr;
        formalism::Literal<formalism::FluentTag> literal = nullptr;
    };

private:
    bool m_enabled;
    bool m_debug_crosscheck;
    formalism::Problem m_problem;
    ApplicableActionGenerator m_applicable_action_generator;
    StateRepository m_state_repository;
    std::vector<std::vector<PreconditionTrigger>> m_positive_triggers_by_predicate;
    std::vector<std::vector<PreconditionTrigger>> m_negative_triggers_by_predicate;
    std::vector<uint8_t> m_ever_tested_ground_actions;
    std::vector<formalism::GroundAction> m_candidate_actions;
    std::vector<formalism::GroundAction> m_partial_completion_actions;
    std::vector<formalism::GroundAction> m_full_applicable_actions;
    iw::AtomIndexList m_changed_add_atom_indices;
    iw::AtomIndexList m_changed_del_atom_indices;
    std::chrono::nanoseconds m_trigger_lookup_time;
    std::chrono::nanoseconds m_partial_completion_time;
    std::chrono::nanoseconds m_debug_crosscheck_time;
    IW1IncrementalFirstApplicabilityStatistics m_statistics;

    void build_trigger_index();
    bool has_tested_action(formalism::GroundAction action) const;
    void ensure_ground_action_capacity(Index action_index);
    void run_debug_crosscheck(const State& state, SearchNode search_node);

public:
    IW1IncrementalActionDiscoveryController(const SearchContext& context, const Options& options);

    [[nodiscard]] bool is_enabled() const { return m_enabled; }
    void on_root_action_fully_enumerated();
    void mark_action_tested(formalism::GroundAction action);
    std::span<const formalism::GroundAction> get_actions_to_expand(const State& state, SearchNode search_node);
    [[nodiscard]] const IW1IncrementalFirstApplicabilityStatistics& get_statistics() const { return m_statistics; }
};

}

#endif
