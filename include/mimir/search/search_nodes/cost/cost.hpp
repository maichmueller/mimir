#ifndef MIMIR_SEARCH_SEARCH_NODES_COST_HPP_
#define MIMIR_SEARCH_SEARCH_NODES_COST_HPP_


#include "../../../buffer/flatbuffers/search_node-cost_generated.h"

#include "../../../buffer/byte_stream.hpp"

#include "../../states.hpp"
#include "../../actions.hpp"

#include <cassert>


namespace mimir
{


/**
 * Data types
*/
enum SearchNodeStatus {NEW = 0, OPEN = 1, CLOSED = 2, DEAD_END = 3};


/**
 * ID class.
 *
 * Define name and template parameters of your own implementation.
*/
template<IsPlanningModeTag P, IsStateTag S>
class CostSearchNodeTag {};


/**
 * Type traits.
*/
template<IsPlanningModeTag P, IsStateTag S>
struct TypeTraits<Builder<CostSearchNodeTag<P, S>>>
{
    using PlanningModeTag = P;
    using StateTag = S;
};

template<IsPlanningModeTag P, IsStateTag S>
struct TypeTraits<View<CostSearchNodeTag<P, S>>>
{
    using PlanningModeTag = P;
    using StateTag = S;
};


/**
 * Define memory layout.
*/
struct CostSearchNodeLayout {
    constexpr static size_t max_align = std::max({alignof(SearchNodeStatus), alignof(int32_t), alignof(int32_t)});

    constexpr static size_t status_offset = 0;
    constexpr static size_t status_padding = compute_amount_padding(sizeof(SearchNodeStatus), alignof(int32_t));
    constexpr static size_t g_value_offset = status_offset + sizeof(SearchNodeStatus) + status_padding;
    constexpr static size_t g_value_padding = compute_amount_padding(g_value_offset + sizeof(int32_t), alignof(int32_t));
    constexpr static size_t parent_state_id_offset = g_value_offset + sizeof(int32_t) + g_value_padding;
    constexpr static size_t parent_state_id_padding = compute_amount_padding(parent_state_id_offset + sizeof(int32_t), max_align);
    
    constexpr static size_t size = parent_state_id_offset + sizeof(int32_t) + parent_state_id_padding;
};


/**
 * Implementation class
*/
template<IsPlanningModeTag P, IsStateTag S>
class Builder<CostSearchNodeTag<P, S>>
    : public IBuilderBase<Builder<CostSearchNodeTag<P, S>>>
{
private:
    using StateView = View<StateDispatcher<S, P>>;
    using ActionView = View<ActionDispatcher<P, S>>;

    /* Define buffer */
    ByteStream m_buffer;

    /* Define data members */
    SearchNodeStatus m_status;
    int32_t m_g_value; 
    int32_t m_parent_state_id;

    /* Implement IBuilderBase interface */
    template<typename>
    friend class IBuilderBase;

    void finish_impl() {
        size_t pos = 0;
        pos += m_buffer.write<SearchNodeStatus>(m_status);
        pos += m_buffer.write_padding(CostSearchNodeLayout::status_padding);
        pos += m_buffer.write<int32_t>(m_g_value);
        pos += m_buffer.write_padding(CostSearchNodeLayout::g_value_padding);
        pos += m_buffer.write<int32_t>(m_parent_state_id);
        pos += m_buffer.write_padding(CostSearchNodeLayout::parent_state_id_padding);
        assert(is_correctly_aligned(pos, CostSearchNodeLayout::max_align));
    }

    void clear_impl() { m_buffer.clear(); }

    [[nodiscard]] uint8_t* get_buffer_pointer_impl() { return m_buffer.get_data(); }
    [[nodiscard]] const uint8_t* get_buffer_pointer_impl() const { return m_buffer.get_data(); }
    [[nodiscard]] uint32_t get_size_impl() const { return CostSearchNodeLayout::size; }

public:
    Builder() { }

    /// @brief Construct a builder with custom default values.
    Builder(SearchNodeStatus status, int g_value, uint32_t parent_state_id)
        : m_status(status), m_g_value(g_value), m_parent_state_id(parent_state_id) {
        this->finish();
    }

    void set_status(SearchNodeStatus status) { m_status = status; }
    void set_g_value(int g_value) { m_g_value = g_value; }
    void set_parent_state_id(uint32_t parent_state_id) { m_parent_state_id = parent_state_id; }
};


/**
 * Implementation class
 *
 * Reads the memory layout generated by the search node builder.
*/
template<IsPlanningModeTag P, IsStateTag S>
class View<CostSearchNodeTag<P, S>>
    : public IView<View<CostSearchNodeTag<P, S>>>
{
private:
    using StateView = View<StateDispatcher<S, P>>;
    using ActionView = View<ActionDispatcher<P, S>>;

    uint8_t* m_data;

    /* Implement IView interface: */
    template<typename>
    friend class IView;

    [[nodiscard]] const uint8_t* get_buffer_pointer_impl() const { return m_data; }

    [[nodiscard]] uint32_t get_size_impl() const { return CostSearchNodeLayout::size; }

public:
    /// @brief Create a view on a SearchNode.
    explicit View(uint8_t* data) : m_data(data) { }

    void set_status(SearchNodeStatus status) {
        assert(m_data);
        read_value<SearchNodeStatus>(m_data + CostSearchNodeLayout::status_offset) = status;
    }

    void set_g_value(int32_t g_value) {
        assert(m_data);
        read_value<int32_t>(m_data + CostSearchNodeLayout::g_value_offset) = g_value;
    }

    [[nodiscard]] SearchNodeStatus get_status() {
        assert(m_data);
        return read_value<SearchNodeStatus>(m_data + CostSearchNodeLayout::status_offset);
    }

    [[nodiscard]] int32_t get_g_value() {
        assert(m_data);
        return read_value<int32_t>(m_data + CostSearchNodeLayout::g_value_offset);
    }

    [[nodiscard]] int32_t get_parent_state_id() {
        assert(m_data);
        return read_value<int32_t>(m_data + CostSearchNodeLayout::parent_state_id_offset);
    }
};


}

#endif