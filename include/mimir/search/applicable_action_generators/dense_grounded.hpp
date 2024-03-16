#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_DENSE_GROUNDED_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_DENSE_GROUNDED_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"

namespace mimir
{

/**
 * Fully specialized implementation class.
 */
template<>
class AAG<GroundedAAGDispatcher<DenseStateTag>> : public IStaticAAG<AAG<GroundedAAGDispatcher<DenseStateTag>>>
{
private:
    using ConstStateView = ConstView<StateDispatcher<DenseStateTag>>;
    using ConstActionView = ConstView<ActionDispatcher<DenseStateTag>>;

    Problem m_problem;
    PDDLFactories& m_pddl_factories;

    /* Implement IStaticAAG interface */
    template<typename>
    friend class IStaticAAG;

    void generate_applicable_actions_impl(ConstStateView state, std::vector<ConstActionView>& out_applicable_actions) {}

public:
    AAG(Problem problem, PDDLFactories& pddl_factories) : m_problem(problem), m_pddl_factories(pddl_factories) {}

    /// @brief Return the action with the given id.
    [[nodiscard]] ConstActionView get_action(size_t action_id) const { throw std::runtime_error("not implemented"); }
};
}

#endif