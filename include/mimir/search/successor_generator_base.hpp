#ifndef MIMIR_SEARCH_SUCCESSOR_GENERATOR_BASE_HPP_
#define MIMIR_SEARCH_SUCCESSOR_GENERATOR_BASE_HPP_

#include "state_base.hpp"
#include "type_traits.hpp"

#include "../formalism/action.hpp"


namespace mimir
{

/// @brief Top-level CRTP based interface for a SuccessorGenerator.
/// @tparam Derived
template<typename Derived>
class SuccessorGeneratorBase : public UncopyableMixin<SuccessorGeneratorBase<Derived>> {
private:
    using Config = typename TypeTraits<Derived>::ConfigType;

    SuccessorGeneratorBase() = default;
    friend Derived;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

public:
    /// @brief Generate all applicable actions for a given state.
    ActionList generate_applicable_actions(const State<Config>& state) {
        return self().generate_applicable_actions_impl(state);
    }
};


/// @brief A concrete successor generator.
template<typename Config>
class SuccessorGenerator : public SuccessorGeneratorBase<SuccessorGenerator<Config>> {
private:
    // Implement Config independent functionality.
};


template<typename Config>
struct TypeTraits<SuccessorGenerator<Config>> {
    using ConfigType = Config;
};



}  // namespace mimir

#endif  // MIMIR_SEARCH_SUCCESSOR_GENERATOR_BASE_HPP_