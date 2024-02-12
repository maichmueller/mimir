#ifndef MIMIR_SEARCH_ACTIONS_BITSET_HPP_
#define MIMIR_SEARCH_ACTIONS_BITSET_HPP_

#include "interface.hpp"


namespace mimir
{

/**
 * Implementation class
*/
template<IsPlanningModeTag P>
class Builder<ActionDispatcher<P, BitsetStateTag>>
    : public IBuilderBase<Builder<ActionDispatcher<P, BitsetStateTag>>>
    , public IActionBuilder<Builder<ActionDispatcher<P, BitsetStateTag>>>
{
private:
    flatbuffers::FlatBufferBuilder m_flatbuffers_builder;

    // The bitset data
    Bitset<uint64_t> m_pos_pre_bitset;
    Bitset<uint64_t> m_neg_pre_bitset;
    Bitset<uint64_t> m_pos_eff_bitset;
    Bitset<uint64_t> m_neg_eff_bitset;

    /* Implement IBuilderBase interface */
    template<typename>
    friend class IBuilderBase;

    void finish_impl() {
        // Genenerate nested data first.
        auto flat_pos_pre_bitset = serialize(m_pos_pre_bitset, this->m_flatbuffers_builder);
        auto flat_neg_pre_bitset = serialize(m_neg_pre_bitset, this->m_flatbuffers_builder);
        auto flat_pos_eff_bitset = serialize(m_pos_eff_bitset, this->m_flatbuffers_builder);
        auto flat_neg_eff_bitset = serialize(m_neg_eff_bitset, this->m_flatbuffers_builder);
        // Generate action data.
        auto offset = CreateActionBitsetFlat(
            this->m_flatbuffers_builder,
            flat_pos_pre_bitset,
            flat_neg_pre_bitset,
            flat_pos_eff_bitset,
            flat_neg_eff_bitset);
        this->m_flatbuffers_builder.FinishSizePrefixed(offset);
    }

    void clear_impl() {
        m_flatbuffers_builder.Clear();
        m_pos_pre_bitset.unset_all(false);
        m_neg_pre_bitset.unset_all(false);
        m_pos_eff_bitset.unset_all(false);
        m_neg_eff_bitset.unset_all(false);
    }

    [[nodiscard]] uint8_t* get_buffer_pointer_impl() { return m_flatbuffers_builder.GetBufferPointer(); }
    [[nodiscard]] const uint8_t* get_buffer_pointer_impl() const { return m_flatbuffers_builder.GetBufferPointer(); }
    [[nodiscard]] uint32_t get_size_impl() const { return read_value<flatbuffers::uoffset_t>(this->get_buffer_pointer()) + sizeof(flatbuffers::uoffset_t); }

    /* Implement IActionBuilder interface */
    template<typename>
    friend class IActionBuilder;

public:
    /// @brief Modify the bitsets, call finish, then copy the buffer to a container and use its returned view.
    [[nodiscard]] Bitset<uint64_t>& get_applicability_positive_precondition_bitset() { return m_pos_pre_bitset; }
    [[nodiscard]] Bitset<uint64_t>& get_applicability_negative_precondition_bitset() { return m_neg_pre_bitset; }
    [[nodiscard]] Bitset<uint64_t>& get_unconditional_positive_effect_bitset() { return m_pos_eff_bitset; }
    [[nodiscard]] Bitset<uint64_t>& get_unconditional_negative_effect_bitset() { return m_neg_eff_bitset; }
};



/**
 * Implementation class
 *
 * Reads the memory layout generated by the search node builder.
*/
template<IsPlanningModeTag P>
class View<ActionDispatcher<P, BitsetStateTag>>
    : public IView<View<ActionDispatcher<P, BitsetStateTag>>>
    , public IActionView<View<ActionDispatcher<P, BitsetStateTag>>>
{
private:
    const uint8_t* m_data;
    const ActionBitsetFlat* m_flatbuffers_view;

    /* Implement IView interface: */
    template<typename>
    friend class IView;

    [[nodiscard]] const uint8_t* get_buffer_pointer_impl() const { return m_data; }

    [[nodiscard]] uint32_t get_size_impl() const {
        assert(m_data && m_flatbuffers_view);
        return read_value<flatbuffers::uoffset_t>(m_data) + sizeof(flatbuffers::uoffset_t);
    }

    /* Implement IActionView interface */
    template<typename>
    friend class IActionView;

    // We probably want to do that differently...
    std::string str_impl() const { return "some_action"; }

public:
    /// @brief Create a view on a DefaultAction.
    explicit View(uint8_t* data)
        : m_data(data)
        , m_flatbuffers_view(data ? GetSizePrefixedActionBitsetFlat(reinterpret_cast<void*>(data)) : nullptr) { }

    [[nodiscard]] ConstBitsetView<uint64_t> get_applicability_positive_precondition_bitset() { return ConstBitsetView<uint64_t>(m_flatbuffers_view->applicability_positive_precondition_bitset()); }
    [[nodiscard]] ConstBitsetView<uint64_t> get_applicability_negative_precondition_bitset() { return ConstBitsetView<uint64_t>(m_flatbuffers_view->applicability_negative_precondition_bitset()); }
    [[nodiscard]] ConstBitsetView<uint64_t> get_unconditional_positive_effect_bitset() { return ConstBitsetView<uint64_t>(m_flatbuffers_view->unconditional_positive_effect_bitset()); };
    [[nodiscard]] ConstBitsetView<uint64_t> get_unconditional_negative_effect_bitset() { return ConstBitsetView<uint64_t>(m_flatbuffers_view->unconditional_negative_effect_bitset()); };
};



}

#endif
