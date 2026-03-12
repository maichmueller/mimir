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

}
