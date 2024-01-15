#include <mimir/common/config.hpp>
#include <mimir/search/state.hpp>

#include <gtest/gtest.h>


namespace mimir::tests
{

TEST(MimirTests, GroundedStateTest) {
    // Instantiate grounded version
    auto grounded_state = search::State<Grounded>();
} 


TEST(MimirTests, LiftedStateTest) {
    // Instantiate lifted version 
    auto lifted_state = search::State<Lifted>();
} 

} 