
#ifndef PYMIMIR_VARIANTS_HPP
#define PYMIMIR_VARIANTS_HPP

#include "init_declarations.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
#include "pymimir.hpp"

namespace pymimir
{

/**
 * We cannot expose the variant types directly because they are not default constructible.
 */

struct TermVariant
{
    Term term;
    explicit TermVariant(const Term& t) : term(t) {}
};

using TermVariantList = std::vector<TermVariant>;

struct FunctionExpressionVariant
{
    FunctionExpression function_expression;
    explicit FunctionExpressionVariant(const FunctionExpression& e) : function_expression(e) {}
};

using FunctionExpressionVariantList = std::vector<FunctionExpressionVariant>;

struct GroundFunctionExpressionVariant
{
    GroundFunctionExpression function_expression;
    explicit GroundFunctionExpressionVariant(const GroundFunctionExpression& e) : function_expression(e) {}
};

using GroundFunctionExpressionVariantList = std::vector<GroundFunctionExpressionVariant>;

inline TermVariantList to_term_variant_list(const TermList& terms) { return ranges::to_vector(terms | std::views::transform(AS_LAMBDA(TermVariant))); }

inline FunctionExpressionVariantList to_function_expression_variant_list(const FunctionExpressionList& function_expressions)
{
    return ranges::to_vector(function_expressions | std::views::transform(AS_LAMBDA(FunctionExpressionVariant)));
}

inline GroundFunctionExpressionVariantList to_ground_function_expression_variant_list(const GroundFunctionExpressionList& ground_function_expressions)
{
    return ranges::to_vector(ground_function_expressions | std::views::transform(AS_LAMBDA(GroundFunctionExpressionVariant)));
}

}

#endif  // PYMIMIR_VARIANTS_HPP
