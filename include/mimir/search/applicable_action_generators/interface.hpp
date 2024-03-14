#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/compile_flags.hpp"

namespace mimir
{

/**
 * Interface class.
 */
class IAAG
{
public:
    virtual ~IAAG() = default;

    /// @brief Generate all applicable actions for a given state.
    virtual void generate_applicable_actions(VState state, VActionList& out_applicable_actions) = 0;

    /// @brief Return the action with the given id.
    [[nodiscard]] virtual VAction get_action(size_t action_id) const = 0;
};

}

#endif
