#ifndef MIMIR_SEARCH_GROUNDED_STATE_BUILDER_HPP_
#define MIMIR_SEARCH_GROUNDED_STATE_BUILDER_HPP_

#include "../state_builder.hpp"


namespace mimir
{

/**
 * No ID class since we have all tags.
*/


/**
 * Implementation class
 *
 * The grounded state builder extends the builder base memory layout as follows:
 *  __________________________________
 * |                |          |      |
 * | data_size_type | state_id | TODO |
 * |________________|__________|______|
 *
 *
*/
template<>
class Builder<State<GroundedTag>> : public BuilderBase<Builder<State<GroundedTag>>>, public StateBuilderBase<Builder<State<GroundedTag>>> {
    state_id_type m_id;

    /* Implement BuilderBase interface */
    data_size_type calculate_size_impl() const {
        return sizeof(state_id_type);
    }

    void finish_impl() {
        this->m_buffer.write(m_id);
    }

    // Give access to the private interface implementations.
    template<typename>
    friend class BuilderBase;

    /* Implement StateBuilderBase interface */
    void set_id_impl(state_id_type id) { m_id = id; }

    // Give access to the private interface implementations.
    template<typename>
    friend class StateBuilderBase;
};

}  // namespace mimir

#endif  // MIMIR_SEARCH_GROUNDED_STATE_BUILDER_HPP_
