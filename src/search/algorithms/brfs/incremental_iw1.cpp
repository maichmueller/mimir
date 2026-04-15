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

#include "incremental_iw1.hpp"

#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/formatter.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/formalism/term.hpp"
#include "mimir/formalism/variable.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
std::string format_ground_action(GroundAction action, Problem problem)
{
    if (!action)
    {
        return "<invalid-action>";
    }

    auto out = std::ostringstream {};
    out << std::tuple<const GroundActionImpl&, const ProblemImpl&, PlanFormatterTag> { *action, *problem, PlanFormatterTag {} };
    return out.str();
}

std::string format_ground_atom(GroundAtom<FluentTag> atom)
{
    auto out = std::ostringstream {};
    out << *atom;
    return out.str();
}

std::string format_ground_action_list(const std::vector<GroundAction>& actions, Problem problem)
{
    auto out = std::ostringstream {};
    out << "[";
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << format_ground_action(actions[i], problem);
    }
    out << "]";
    return out.str();
}

std::string format_ground_atom_indices(const iw::AtomIndexList& atom_indices, Problem problem)
{
    auto out = std::ostringstream {};
    out << "[";
    for (size_t i = 0; i < atom_indices.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << format_ground_atom(problem->get_repositories().get_ground_atom<FluentTag>(atom_indices[i]));
    }
    out << "]";
    return out.str();
}

bool unify_trigger_with_ground_atom(const GroundAtom<FluentTag>& atom, const IW1IncrementalActionDiscoveryController::PreconditionTrigger& trigger, PartialGroundActionSeed& out_seed)
{
    const auto literal_atom = trigger.literal->get_atom();
    if (literal_atom->get_predicate() != atom->get_predicate())
    {
        return false;
    }

    const auto action_schema = trigger.action_schema;
    const auto action_arity = action_schema->get_arity();

    out_seed.action_schema = action_schema;
    out_seed.bound_parameter_object_indices.assign(action_arity, 0);
    out_seed.parameter_is_bound.assign(action_arity, 0);

    const auto& literal_terms = literal_atom->get_terms();
    const auto& ground_objects = atom->get_objects();
    if (literal_terms.size() != ground_objects.size())
    {
        return false;
    }

    for (size_t term_index = 0; term_index < literal_terms.size(); ++term_index)
    {
        const auto& term_variant = literal_terms[term_index]->get_variant();
        const auto object_index = ground_objects[term_index]->get_index();

        if (const auto* variable = std::get_if<Variable>(&term_variant))
        {
            const auto parameter_index = (*variable)->get_parameter_index();
            if (out_seed.parameter_is_bound[parameter_index])
            {
                if (out_seed.bound_parameter_object_indices[parameter_index] != object_index)
                {
                    return false;
                }
                continue;
            }

            out_seed.parameter_is_bound[parameter_index] = 1;
            out_seed.bound_parameter_object_indices[parameter_index] = object_index;
            continue;
        }

        const auto required_object = std::get<Object>(term_variant);
        if (required_object->get_index() != object_index)
        {
            return false;
        }
    }

    return true;
}
}

IW1IncrementalActionDiscoveryController::IW1IncrementalActionDiscoveryController(const SearchContext& context, const Options& options) :
    m_enabled(options.iw1_incremental_first_applicability),
    m_debug_crosscheck(options.iw1_incremental_first_applicability_debug_crosscheck),
    m_problem(context->get_problem()),
    m_applicable_action_generator(context->get_applicable_action_generator()),
    m_state_repository(context->get_state_repository()),
    m_positive_triggers_by_predicate(),
    m_negative_triggers_by_predicate(),
    m_ever_tested_ground_actions(),
    m_candidate_actions(),
    m_partial_completion_actions(),
    m_full_applicable_actions(),
    m_changed_add_atom_indices(),
    m_changed_del_atom_indices(),
    m_trigger_lookup_time(std::chrono::nanoseconds::zero()),
    m_partial_completion_time(std::chrono::nanoseconds::zero()),
    m_debug_crosscheck_time(std::chrono::nanoseconds::zero()),
    m_statistics()
{
    if (m_enabled)
    {
        build_trigger_index();
    }
}

void IW1IncrementalActionDiscoveryController::build_trigger_index()
{
    const auto& fluent_predicate_repository =
        boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<PredicateImpl<FluentTag>> {});
    m_positive_triggers_by_predicate.resize(fluent_predicate_repository.size());
    m_negative_triggers_by_predicate.resize(fluent_predicate_repository.size());

    for (const auto action_schema : m_problem->get_domain()->get_actions())
    {
        for (const auto literal : action_schema->get_conjunctive_condition()->get_literals<FluentTag>())
        {
            const auto predicate_index = literal->get_atom()->get_predicate()->get_index();
            auto trigger = PreconditionTrigger { action_schema, literal };
            if (literal->get_polarity())
            {
                m_positive_triggers_by_predicate[predicate_index].push_back(trigger);
            }
            else
            {
                m_negative_triggers_by_predicate[predicate_index].push_back(trigger);
            }
        }
    }
}

bool IW1IncrementalActionDiscoveryController::has_tested_action(GroundAction action) const
{
    const auto action_index = action->get_index();
    return action_index < m_ever_tested_ground_actions.size() && m_ever_tested_ground_actions[action_index];
}

void IW1IncrementalActionDiscoveryController::ensure_ground_action_capacity(Index action_index)
{
    if (action_index >= m_ever_tested_ground_actions.size())
    {
        m_ever_tested_ground_actions.resize(action_index + 1, 0);
    }
}

void IW1IncrementalActionDiscoveryController::on_root_action_fully_enumerated()
{
    m_statistics.set_num_root_actions_fully_enumerated(m_statistics.get_num_root_actions_fully_enumerated() + 1);
}

void IW1IncrementalActionDiscoveryController::mark_action_tested(GroundAction action)
{
    ensure_ground_action_capacity(action->get_index());
    m_ever_tested_ground_actions[action->get_index()] = 1;
}

std::span<const GroundAction> IW1IncrementalActionDiscoveryController::get_actions_to_expand(const State& state, SearchNode search_node)
{
    if (!m_enabled)
    {
        return {};
    }

    if (search_node.parent_state == std::numeric_limits<Index>::max() || search_node.incoming_action == kInvalidGroundActionIndex)
    {
        throw std::invalid_argument("IW1IncrementalActionDiscoveryController::get_actions_to_expand requires a non-root search node.");
    }

    const auto parent_packed_state = m_state_repository->get_packed_state(search_node.parent_state);
    const auto parent_state = m_state_repository->get_state(*parent_packed_state);
    const auto& ground_action_repository =
        boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto parent_action = ground_action_repository.at(search_node.incoming_action);

    m_state_repository->collect_action_change_effect_fluent_atom_indices(parent_state, parent_action, m_changed_add_atom_indices, m_changed_del_atom_indices);

    m_statistics.set_num_non_root_states_using_incremental_path(m_statistics.get_num_non_root_states_using_incremental_path() + 1);
    m_statistics.set_num_changed_atoms_processed(m_statistics.get_num_changed_atoms_processed() + m_changed_add_atom_indices.size()
                                                 + m_changed_del_atom_indices.size());

    m_candidate_actions.clear();
    auto local_candidate_action_indices = std::unordered_set<Index> {};
    auto partial_seed = PartialGroundActionSeed {};

    const auto process_changed_atom_indices =
        [&](const iw::AtomIndexList& changed_atom_indices, const std::vector<std::vector<PreconditionTrigger>>& triggers_by_predicate)
    {
        for (const auto atom_index : changed_atom_indices)
        {
            const auto changed_atom = m_problem->get_repositories().get_ground_atom<FluentTag>(atom_index);
            const auto predicate_index = changed_atom->get_predicate()->get_index();
            const auto& triggers = triggers_by_predicate[predicate_index];

            m_statistics.set_num_trigger_records_visited(m_statistics.get_num_trigger_records_visited() + triggers.size());

            for (const auto& trigger : triggers)
            {
                if (!unify_trigger_with_ground_atom(changed_atom, trigger, partial_seed))
                {
                    continue;
                }

                m_statistics.set_num_partial_seeds_created(m_statistics.get_num_partial_seeds_created() + 1);

                m_partial_completion_actions.clear();
                const auto partial_completion_start = std::chrono::steady_clock::now();
                m_applicable_action_generator->create_applicable_actions_from_partial_binding(state, partial_seed, m_partial_completion_actions);
                const auto partial_completion_time =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - partial_completion_start);
                m_partial_completion_time += partial_completion_time;
                m_statistics.set_partial_completion_time(m_partial_completion_time);
                m_statistics.set_num_ground_actions_returned_by_partial_completion(
                    m_statistics.get_num_ground_actions_returned_by_partial_completion() + m_partial_completion_actions.size());

                for (const auto action : m_partial_completion_actions)
                {
                    if (!local_candidate_action_indices.insert(action->get_index()).second)
                    {
                        m_statistics.set_num_local_duplicate_candidates_removed(
                            m_statistics.get_num_local_duplicate_candidates_removed() + 1);
                        continue;
                    }

                    if (has_tested_action(action))
                    {
                        m_statistics.set_num_already_tested_actions_skipped(m_statistics.get_num_already_tested_actions_skipped() + 1);
                        continue;
                    }

                    m_candidate_actions.push_back(action);
                }
            }
        }
    };

    const auto trigger_lookup_start = std::chrono::steady_clock::now();
    process_changed_atom_indices(m_changed_add_atom_indices, m_positive_triggers_by_predicate);
    process_changed_atom_indices(m_changed_del_atom_indices, m_negative_triggers_by_predicate);
    const auto trigger_lookup_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - trigger_lookup_start);
    m_trigger_lookup_time += trigger_lookup_time;
    m_statistics.set_trigger_lookup_time(m_trigger_lookup_time);

    if (m_candidate_actions.empty())
    {
        m_statistics.set_num_non_root_states_with_zero_returned_actions(
            m_statistics.get_num_non_root_states_with_zero_returned_actions() + 1);
    }

    if (m_debug_crosscheck)
    {
        run_debug_crosscheck(state, search_node);
    }

    return m_candidate_actions;
}

void IW1IncrementalActionDiscoveryController::run_debug_crosscheck(const State& state, SearchNode search_node)
{
    const auto debug_crosscheck_start = std::chrono::steady_clock::now();

    m_full_applicable_actions.clear();
    for (const auto action : m_applicable_action_generator->create_applicable_action_generator(state))
    {
        if (!has_tested_action(action))
        {
            m_full_applicable_actions.push_back(action);
        }
    }

    auto incremental_indices = std::unordered_set<Index> {};
    for (const auto action : m_candidate_actions)
    {
        incremental_indices.insert(action->get_index());
    }

    auto missing_actions = std::vector<GroundAction> {};
    auto spurious_actions = std::vector<GroundAction> {};
    auto baseline_indices = std::unordered_set<Index> {};

    for (const auto action : m_full_applicable_actions)
    {
        baseline_indices.insert(action->get_index());
        if (!incremental_indices.contains(action->get_index()))
        {
            missing_actions.push_back(action);
        }
    }

    for (const auto action : m_candidate_actions)
    {
        if (!baseline_indices.contains(action->get_index()))
        {
            spurious_actions.push_back(action);
        }
    }

    const auto debug_crosscheck_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - debug_crosscheck_start);
    m_debug_crosscheck_time += debug_crosscheck_time;
    m_statistics.set_debug_crosscheck_time(m_debug_crosscheck_time);

    if (missing_actions.empty() && spurious_actions.empty())
    {
        return;
    }

    const auto& ground_action_repository =
        boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto parent_action = ground_action_repository.at(search_node.incoming_action);

    auto message = std::ostringstream {};
    message << "IW(1) incremental first-applicability cross-check failed.\n";
    message << "state_id: " << state.get_index() << "\n";
    message << "incoming_parent_action: " << format_ground_action(parent_action, m_problem) << "\n";
    message << "changed_literals: add=" << format_ground_atom_indices(m_changed_add_atom_indices, m_problem)
            << " del=" << format_ground_atom_indices(m_changed_del_atom_indices, m_problem) << "\n";
    message << "incremental_candidates: " << format_ground_action_list(m_candidate_actions, m_problem) << "\n";
    message << "missing_actions: " << format_ground_action_list(missing_actions, m_problem) << "\n";
    message << "spurious_actions: " << format_ground_action_list(spurious_actions, m_problem) << "\n";

    throw std::runtime_error(message.str());
}

}
