#include <mimir/search/config.hpp>
#include <mimir/search/heuristics/zero.hpp>

#include <gtest/gtest.h>


namespace mimir::tests
{

TEST(MimirTests, SearchHeuristicsZeroTest) {
    // Instantiate ground version
    auto zero_heuristic_grounded = ZeroHeuristic<Grounded>();

    // Instantiate lifted version
    auto zero_heuristic_lifted = ZeroHeuristic<Lifted>();
}

}