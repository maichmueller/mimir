#ifndef MIMIR_SEARCH_ACTIONS_DEFAULT_HPP_
#define MIMIR_SEARCH_ACTIONS_DEFAULT_HPP_

#include "template.hpp"

#include "../../buffer/flatbuffers/search/actions/default_generated.h"


namespace mimir
{

/**
 * Derived ID class.
 *
 * Define name and template parameters of your own implementation.
*/
class DefaultActionTag : public ActionBaseTag {};


/**
 * Dispatcher class.
 *
 * Define the required template arguments of your implementation.
*/
template<IsActionTag A, IsPlanningModeTag P, IsStateTag S>
struct is_action_dispatcher<ActionDispatcher<A, P, S>> : std::true_type {};


/**
 * Aliases
*/
template<IsPlanningModeTag P, IsStateTag S>
using DefaultActionBuilder = Builder<ActionDispatcher<DefaultActionTag, P, S>>;

template<IsPlanningModeTag P, IsStateTag S>
using DefaultActionView = View<ActionDispatcher<DefaultActionTag, P, S>>;


/**
 * Type traits.
*/
template<IsPlanningModeTag P, IsStateTag S>
struct TypeTraits<DefaultActionBuilder<P, S>> {
    using PlanningModeTag = P;
    using StateTag = S;
    using TypeFlatBuilder = DefaultActionFlatBuilder;
};

template<IsPlanningModeTag P, IsStateTag S>
struct TypeTraits<DefaultActionView<P, S>> {
    using PlanningModeTag = P;
    using StateTag = S;
};


/**
 * Interface class
*/
template<typename Derived>
class DefaultActionBuilderBase {
private:
    using P = typename TypeTraits<Derived>::PlanningModeTag;
    using S = typename TypeTraits<Derived>::StateTag;

    DefaultActionBuilderBase() = default;
    friend Derived;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

public:
};


/**
 * Implementation class
 *
 * The search node builder extends the builder base memory layout as follows:
 *  ____________________________________________________________________
 * |                |        |         |              |                 |
 * | data_size_type | status | g_value | parent_state | creating_action |
 * |________________|________|_________|______________|_________________|
*/
template<IsPlanningModeTag P, IsStateTag S>
class Builder<ActionDispatcher<DefaultActionTag, P, S>>
    : public BuilderBase<Builder<ActionDispatcher<DefaultActionTag, P, S>>>
    , public DefaultActionBuilderBase<Builder<ActionDispatcher<DefaultActionTag, P, S>>> {
private:
    //mimir::Bitset applicability_positive_precondition_bitset_;
    //mimir::Bitset applicability_negative_precondition_bitset_;
    //mimir::Bitset unconditional_positive_effect_bitset_;
    //mimir::Bitset unconditional_negative_effect_bitset_;

    /* Implement BuilderBase interface */
    void finish_impl() {
        // TODO:
    }

    uint8_t* get_buffer_pointer_impl() {
        // TODO: implement
        return nullptr;
    }

    const uint8_t* get_buffer_pointer_impl() const {
        // TODO: implement
        return nullptr;
    }

    void clear_impl() {
        // TODO: implement
    }

    // Give access to the private interface implementations.
    template<typename>
    friend class BuilderBase;

    /* Implement DefaultActionBuilderBase interface */

    // Give access to the private interface implementations.
    template<typename>
    friend class DefaultActionBuilderBase;

public:
};


/**
 * Interface class
*/
template<typename Derived>
class DefaultActionViewBase {
private:
    using P = typename TypeTraits<Derived>::PlanningModeTag;
    using S = typename TypeTraits<Derived>::StateTag;

    DefaultActionViewBase() = default;
    friend Derived;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

public:
    /* Mutable getters. */

    /* Immutable getters. */
    std::string str() const { return self().str_impl(); }
};


/**
 * Implementation class
 *
 * Reads the memory layout generated by the search node builder.
*/
template<IsPlanningModeTag P, IsStateTag S>
class View<ActionDispatcher<DefaultActionTag, P, S>>
    : public ViewBase<View<ActionDispatcher<DefaultActionTag, P, S>>>
    , public DefaultActionViewBase<View<ActionDispatcher<DefaultActionTag, P, S>>> {
private:
    /* Implement ViewBase interface: */
    template<typename>
    friend class ViewBase;

    /* Implement DefaultActionViewBase interface */
    template<typename>
    friend class DefaultActionViewBase;

public:
    /// @brief Create a view on a DefaultAction.
    explicit View(uint8_t* data) : ViewBase<View<ActionDispatcher<DefaultActionTag, P, S>>>(data) { }

    std::string str_impl() const { return "some_action"; }
};



}  // namespace mimir

#endif  // MIMIR_SEARCH_ACTIONS_DEFAULT_HPP_
