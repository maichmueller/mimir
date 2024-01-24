#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATOR_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATOR_HPP_

#include "state.hpp"
#include "grounded/state_view.hpp"
#include "lifted/state_view.hpp"
#include "type_traits.hpp"

#include "../formalism/problem/declarations.hpp"


namespace mimir
{

/**
 * Interface class.
*/
template<typename Derived>
class ApplicableActionGeneratorBase : public UncopyableMixin<ApplicableActionGeneratorBase<Derived>> {
private:
    using P = typename TypeTraits<Derived>::PlanningModeTag;

    ApplicableActionGeneratorBase() = default;
    friend Derived;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

public:
    /// @brief Generate all applicable actions for a given state.
    void generate_applicable_actions(View<State<P>> state, GroundActionList& out_applicable_actions) {
        self().generate_applicable_actions_impl(state, out_applicable_actions);
    }
};


/**
 * ID class.
 *
 * Derive from it to provide your own implementation of a applicable action generator.
*/
struct ApplicableActionGeneratorBaseTag {};

template<typename DerivedTag>
concept IsApplicableActionGeneratorTag = std::derived_from<DerivedTag, ApplicableActionGeneratorBaseTag>;


/**
 * Wrapper class.
 *
 * Wrap the tag and the planning mode to be able to pass
 * the planning mode used in the algorithm.
*/
template<IsApplicableActionGeneratorTag A, IsPlanningModeTag P>
struct WrappedApplicableActionGeneratorTag {
    using ApplicableActionGeneratorTag = A;
    using PlanningModeTag = P;
};

template<typename T>
concept IsWrappedApplicableActionGeneratorTag = requires {
    typename T::PlanningModeTag;
    typename T::ApplicableActionGeneratorTag;
};


/**
 * General implementation class.
 *
 * Spezialize it with your derived tag to provide your own implementation of an applicable action generator.
*/
template<IsWrappedApplicableActionGeneratorTag A>
class ApplicableActionGenerator : public ApplicableActionGeneratorBase<ApplicableActionGenerator<A>> { };



}  // namespace mimir

#endif  // MIMIR_SEARCH_APPLICABLE_ACTION_GENERATOR_HPP_
