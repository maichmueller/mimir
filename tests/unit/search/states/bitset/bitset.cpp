#include <mimir/search/states.hpp>

#include <gtest/gtest.h>


namespace mimir::tests
{

TEST(MimirTests, SearchStatesBitsetTest) {
    // Build a state.
    /*
    auto builder = Builder<StateDispatcher<BitsetStateTag, GroundedTag>>();
    builder.set_id(5);  // 4 bytes
    auto& bitset = builder.get_atoms_bitset();
    bitset.set(20); 
    bitset.set(100);  // 16 bytes
    builder.finish();
    EXPECT_NE(builder.get_buffer_pointer(), nullptr);
    EXPECT_EQ(builder.get_size(), 64);

    // View the data generated in the builder.
    auto view = View<StateDispatcher<BitsetStateTag, GroundedTag>>(builder.get_buffer_pointer());
    EXPECT_EQ(view.get_id(), 5);
    EXPECT_EQ(view.get_size(), 64);
    EXPECT_FALSE(view.get_atoms_bitset().get(0));
    EXPECT_TRUE(view.get_atoms_bitset().get(20));
    EXPECT_FALSE(view.get_atoms_bitset().get(64));
    EXPECT_TRUE(view.get_atoms_bitset().get(100));
    */
}

}
