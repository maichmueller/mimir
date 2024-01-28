#ifndef MIMIR_SEARCH_STATES_BITSET_LIFTED_HPP_
#define MIMIR_SEARCH_STATES_BITSET_LIFTED_HPP_

#include "bitset.hpp"

#include "../../buffer/flatbuffers/state-bitset-lifted_generated.h"


namespace mimir
{

/**
 * Implementation class
*/
template<>
class Builder<StateDispatcher<BitsetStateTag, LiftedTag>>
    : public BuilderBase<Builder<StateDispatcher<BitsetStateTag, LiftedTag>>>
    , public StateBuilderBase<Builder<StateDispatcher<BitsetStateTag, LiftedTag>>>
{
private:
    flatbuffers::FlatBufferBuilder m_flatbuffers_builder;

    uint32_t m_id;
    BitsetBuilder<uint64_t> m_atoms_bitset;


    /* Implement BuilderBase interface */
    template<typename>
    friend class BuilderBase;

    void finish_impl() {
        auto created_atoms_vec = this->m_flatbuffers_builder.CreateVector(m_atoms_bitset.get_data());
        auto bitset = CreateBitsetFlat(m_flatbuffers_builder, m_atoms_bitset.get_data().size(), created_atoms_vec);
        auto builder = StateBitsetLiftedFlatBuilder(this->m_flatbuffers_builder);
        builder.add_id(m_id);
        builder.add_atoms(bitset);
        this->m_flatbuffers_builder.FinishSizePrefixed(builder.Finish());
    }

    void clear_impl() {
        m_flatbuffers_builder.Clear();
        m_atoms_bitset.clear();
    }

    [[nodiscard]] uint8_t* get_buffer_pointer_impl() { return m_flatbuffers_builder.GetBufferPointer(); }
    [[nodiscard]] const uint8_t* get_buffer_pointer_impl() const { return m_flatbuffers_builder.GetBufferPointer(); }
    [[nodiscard]] uint32_t get_size_impl() const { return *reinterpret_cast<const flatbuffers::uoffset_t*>(this->get_buffer_pointer()) + sizeof(flatbuffers::uoffset_t); }


    /* Implement StateBuilderBase interface */
    template<typename>
    friend class StateBuilderBase;

    void set_id_impl(uint32_t id) { m_id = id; }

public:
    void set_num_atoms(size_t num_atoms) { m_atoms_bitset.set_num_bits(num_atoms); }
};


/**
 * Implementation class
 *
 * Reads the memory layout generated by the lifted state builder.
*/
template<>
class View<StateDispatcher<BitsetStateTag, LiftedTag>>
    : public ViewBase<View<StateDispatcher<BitsetStateTag, LiftedTag>>>
    , public StateViewBase<View<StateDispatcher<BitsetStateTag, LiftedTag>>>
{
private:
    const StateBitsetLiftedFlat* m_flatbuffers_view;

    /* Implement ViewBase interface */
    template<typename>
    friend class ViewBase;

    [[nodiscard]] bool are_equal_impl(const View& other) const {
        // TODO: implement when we have data members.
        return true;
    }

    /// @brief Hash the representative data.
    [[nodiscard]] size_t hash_impl() const {
        // TODO: implement when we have data members.
        return 0;
    }


    /* Implement SearchNodeViewBase interface */
    template<typename>
    friend class StateViewBase;

    [[nodiscard]] uint32_t get_id_impl() const { return m_flatbuffers_view->id(); }

public:
    explicit View(uint8_t* data)
        : ViewBase<View<StateDispatcher<BitsetStateTag, LiftedTag>>>(data)
        , m_flatbuffers_view(data ? GetSizePrefixedStateBitsetLiftedFlat(reinterpret_cast<void*>(data)) : nullptr) { }

    [[nodiscard]] BitsetView get_atoms() const { return BitsetView(m_flatbuffers_view->atoms()); }
};


}  // namespace mimir

#endif  // MIMIR_SEARCH_STATES_BITSET_LIFTED_HPP_
