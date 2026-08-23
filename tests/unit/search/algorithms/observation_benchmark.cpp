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

/// What each observation mode costs, in wall time and in retained bytes, relative to a search that
/// observes nothing beyond its statistics.
///
/// Memory is accounted from the containers the observation actually holds rather than from process
/// RSS: RSS is dominated by the state repository and the novelty tables, which every mode pays for
/// equally, and would bury the thing being measured.
///
/// Usage:
///   observation_benchmark [<domain.pddl> <problem.pddl> [max_arity] [repetitions]]
/// With no arguments it runs the gripper instance shipped with the tests.

#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/search_context.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace mimir::search;
using namespace mimir::formalism;

namespace
{
namespace fs = std::filesystem;

struct Mode
{
    const char* name;
    brfs::ObservationOptions options;
    /// Modes that keep nothing run with the plain default handler, which is the baseline to beat.
    bool use_observation_handler = true;
};

struct Measurement
{
    std::string name;
    double milliseconds;
    uint64_t num_expanded;
    uint64_t num_generated;
    size_t num_tree_nodes;
    size_t num_transitions;
    size_t retained_bytes;
};

/// @brief The bytes the observation holds onto, counting the heap each optional atom list owns.
size_t compute_retained_bytes(const brfs::Observation& observation)
{
    auto bytes = observation.get_search_tree().get_num_nodes() * sizeof(brfs::SearchTreeNode);
    bytes += observation.get_num_action_effect_summaries() * (sizeof(brfs::GroundActionEffectSummary) + sizeof(mimir::Index));

    for (const auto& transition : observation.get_transitions())
    {
        bytes += sizeof(brfs::TransitionObservation);
        if (transition.novel_fluent_atom_indices.has_value())
        {
            bytes += transition.novel_fluent_atom_indices->capacity() * sizeof(mimir::Index);
        }
        if (transition.realized_added_fluent_atom_indices.has_value())
        {
            bytes += transition.realized_added_fluent_atom_indices->capacity() * sizeof(mimir::Index);
        }
        if (transition.realized_deleted_fluent_atom_indices.has_value())
        {
            bytes += transition.realized_deleted_fluent_atom_indices->capacity() * sizeof(mimir::Index);
        }
    }

    return bytes;
}

std::vector<Mode> make_modes()
{
    auto modes = std::vector<Mode> {};

    modes.push_back(Mode { "statistics only", brfs::ObservationOptions {}, false });

    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        modes.push_back(Mode { "search tree", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        modes.push_back(Mode { "admitted transitions", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        options.capture_rejected_transitions = true;
        modes.push_back(Mode { "+ rejected transitions", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        options.capture_rejected_transitions = true;
        options.capture_novel_witnesses = true;
        modes.push_back(Mode { "+ novelty witnesses", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        options.capture_rejected_transitions = true;
        options.capture_action_effect_summaries = true;
        modes.push_back(Mode { "+ effect summaries", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        options.capture_rejected_transitions = true;
        options.capture_realized_effects = true;
        modes.push_back(Mode { "+ realized effects", options });
    }
    {
        auto options = brfs::ObservationOptions {};
        options.capture_search_tree = true;
        options.capture_admitted_transitions = true;
        options.capture_rejected_transitions = true;
        options.capture_novel_witnesses = true;
        options.capture_action_effect_summaries = true;
        options.capture_realized_effects = true;
        modes.push_back(Mode { "everything", options });
    }

    return modes;
}

brfs::Options make_search_options(const SearchContext& context, size_t arity, brfs::EventHandler event_handler)
{
    auto options = brfs::Options {};
    options.event_handler = std::move(event_handler);
    options.stop_if_goal = false;  ///< exhaust, so every mode observes the same search
    options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(
        arity,
        boost::hana::at_key(context->get_problem()->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {}).size());
    return options;
}

/// @brief Time every mode over one shared, warmed-up context per repetition.
///
/// Sharing the context is what makes the modes comparable. Two contexts parsed from the same files
/// do not agree on ground-action indices, so per-mode contexts would have each mode observing a
/// slightly different search and the counts would drift for reasons that have nothing to do with
/// capture. The throwaway warm-up run fills the state repository first, so no single mode pays for
/// interning the whole space on everyone else's behalf.
std::vector<Measurement> run_all_modes(const fs::path& domain_filepath, const fs::path& problem_filepath, size_t arity, size_t repetitions)
{
    const auto modes = make_modes();
    auto measurements = std::vector<Measurement> {};
    measurements.reserve(modes.size());
    for (const auto& mode : modes)
    {
        measurements.push_back(Measurement { mode.name, 0.0, 0, 0, 0, 0, 0 });
    }

    for (size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        const auto context =
            SearchContextImpl::create(domain_filepath, problem_filepath, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));

        const auto warmup_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());
        (void) brfs::find_solution(context, make_search_options(context, arity, warmup_handler));

        for (size_t mode_index = 0; mode_index < modes.size(); ++mode_index)
        {
            const auto& mode = modes[mode_index];
            const auto handler = mode.use_observation_handler ?
                                     brfs::EventHandler(brfs::ObservationEventHandlerImpl::create(context->get_problem(), mode.options)) :
                                     brfs::EventHandler(brfs::DefaultEventHandlerImpl::create(context->get_problem()));

            const auto start = std::chrono::steady_clock::now();
            (void) brfs::find_solution(context, make_search_options(context, arity, handler));
            const auto elapsed = std::chrono::steady_clock::now() - start;

            auto& measurement = measurements[mode_index];
            measurement.milliseconds += std::chrono::duration<double, std::milli>(elapsed).count();
            measurement.num_expanded = handler->get_statistics().get_num_expanded();
            measurement.num_generated = handler->get_statistics().get_num_generated();
            if (mode.use_observation_handler)
            {
                const auto& observation = std::static_pointer_cast<brfs::ObservationEventHandlerImpl>(handler)->get_observation();
                measurement.num_tree_nodes = observation.get_search_tree().get_num_nodes();
                measurement.num_transitions = observation.get_transitions().size();
                measurement.retained_bytes = compute_retained_bytes(observation);
            }
        }
    }

    for (auto& measurement : measurements)
    {
        measurement.milliseconds /= static_cast<double>(repetitions);
    }
    return measurements;
}
}

int main(int argc, char** argv)
{
    auto domain_filepath = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    auto problem_filepath = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    auto arity = size_t(2);
    auto repetitions = size_t(5);

    if (argc >= 3)
    {
        domain_filepath = fs::path(argv[1]);
        problem_filepath = fs::path(argv[2]);
    }
    if (argc >= 4)
    {
        arity = static_cast<size_t>(std::stoul(argv[3]));
    }
    if (argc >= 5)
    {
        repetitions = static_cast<size_t>(std::stoul(argv[4]));
    }

    std::cout << "Observation benchmark\n"
              << "  domain      : " << domain_filepath << "\n"
              << "  problem     : " << problem_filepath << "\n"
              << "  IW arity    : " << arity << "\n"
              << "  repetitions : " << repetitions << "\n\n";

    const auto measurements = run_all_modes(domain_filepath, problem_filepath, arity, repetitions);
    const auto baseline_ms = measurements.front().milliseconds;

    std::cout << std::left << std::setw(24) << "mode" << std::right << std::setw(12) << "time (ms)" << std::setw(10) << "vs base" << std::setw(12)
              << "nodes" << std::setw(14) << "transitions" << std::setw(14) << "retained KiB" << "\n";
    std::cout << std::string(86, '-') << "\n";

    for (const auto& measurement : measurements)
    {
        std::cout << std::left << std::setw(24) << measurement.name << std::right << std::setw(12) << std::fixed << std::setprecision(2)
                  << measurement.milliseconds << std::setw(9) << std::setprecision(2)
                  << (baseline_ms > 0.0 ? measurement.milliseconds / baseline_ms : 0.0) << "x" << std::setw(12) << measurement.num_tree_nodes
                  << std::setw(14) << measurement.num_transitions << std::setw(14) << std::setprecision(1)
                  << (static_cast<double>(measurement.retained_bytes) / 1024.0) << "\n";
    }

    std::cout << "\nsearch size: " << measurements.front().num_expanded << " expanded, " << measurements.front().num_generated << " generated\n";
    return 0;
}
