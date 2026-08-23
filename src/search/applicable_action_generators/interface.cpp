#include "mimir/search/applicable_action_generators/interface.hpp"

#include "mimir/algorithms/BS_thread_pool.hpp"

#include <stdexcept>

namespace mimir::search
{

std::vector<formalism::GroundAction> IApplicableActionGenerator::create_applicable_action_list_parallel(const State&, BS::thread_pool&)
{
    throw std::logic_error(
        "IApplicableActionGenerator::create_applicable_action_list_parallel: parallel applicable-action generation is not supported.");
}

void IApplicableActionGenerator::create_applicable_actions_from_partial_binding(const State&,
                                                                                const PartialGroundActionSeed&,
                                                                                std::vector<formalism::GroundAction>&)
{
    throw std::logic_error(
        "IApplicableActionGenerator::create_applicable_actions_from_partial_binding: partial-binding completion is not supported.");
}

ParallelRelaxedBeamSuccessorGenerationResult IApplicableActionGenerator::create_relaxed_parallel_beam_successor_candidates(
    const State&,
    ContinuousCost,
    DiscreteCost,
    BS::thread_pool&,
    StateRepositoryImpl&,
    const PruningStrategy&,
    BeamNoveltyMode,
    const LayerOrderingStrategy&,
    uint32_t,
    bool,
    bool,
    uint64_t)
{
    throw std::logic_error(
        "IApplicableActionGenerator::create_relaxed_parallel_beam_successor_candidates: relaxed parallel beam successor generation is not supported.");
}

}
