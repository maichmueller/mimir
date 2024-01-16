#ifndef MIMIR_FORMALISM_ACTION_HPP_
#define MIMIR_FORMALISM_ACTION_HPP_

#include "action_schema.hpp"
#include "bitset.hpp"
#include "implication.hpp"
#include "literal.hpp"
#include "term.hpp"

#include "../common/mixins.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace mimir
{
    class Action : public FormattingMixin<Action>
    {
      private:
        Bitset applicability_positive_precondition_bitset_;
        Bitset applicability_negative_precondition_bitset_;
        Bitset unconditional_positive_effect_bitset_;
        Bitset unconditional_negative_effect_bitset_;
        std::vector<Bitset> conditional_positive_precondition_bitsets_;
        std::vector<Bitset> conditional_negative_precondition_bitsets_;
        std::vector<Bitset> conditional_positive_effect_bitsets_;
        std::vector<Bitset> conditional_negative_effect_bitsets_;
        TermList arguments_;
        LiteralList applicability_precondition_;
        LiteralList unconditional_effect_;
        ImplicationList conditional_effect_;
        ActionSchema schema_;
        double cost_;

        void initialize_precondition();

        void initialize_effect();

      public:
        Action();

        Action(const ActionSchema& schema,
               TermList&& arguments,
               LiteralList&& precondition,
               LiteralList&& unconditional_effect,
               ImplicationList&& conditional_effect,
               double cost);

        // Action(const ActionSchema& schema, TermList&& arguments, int32_t cost = 1);

        // Action(const ActionSchema& schema, const ParameterAssignment& assignment);

        bool is_valid() const;

        const ActionSchema& get_schema() const;

        const TermList& get_arguments() const;

        const LiteralList& get_precondition() const;

        const LiteralList& get_unconditional_effect() const;

        const ImplicationList& get_conditional_effect() const;

        double get_cost() const;

        // bool is_applicable(const State& state) const;

        // State apply(const State& state) const;

        bool operator<(const Action& other) const;
        bool operator>(const Action& other) const;
        bool operator==(const Action& other) const;
        bool operator!=(const Action& other) const;
        bool operator<=(const Action& other) const;
    };

    using ActionList = std::vector<Action>;

}  // namespace mimir

namespace std
{
    // Inject comparison and hash functions to make pointers behave appropriately with ordered and unordered datastructures
    template<>
    struct hash<mimir::Action>
    {
        std::size_t operator()(const mimir::Action& action) const;
    };

    template<>
    struct less<mimir::Action>
    {
        bool operator()(const mimir::Action& left_action, const mimir::Action& right_action) const;
    };

    template<>
    struct equal_to<mimir::Action>
    {
        bool operator()(const mimir::Action& left_action, const mimir::Action& right_action) const;
    };

}  // namespace std

#endif  // MIMIR_FORMALISM_ACTION_HPP_
