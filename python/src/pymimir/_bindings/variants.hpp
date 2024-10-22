
#ifndef PYMIMIR_VARIANTS_HPP
#define PYMIMIR_VARIANTS_HPP

#include <pybind11/pybind11.h>
#include "init_declarations.hpp"
namespace py = pybind11;
#include "mimir/mimir.hpp"

namespace mimir::pymimir {

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

inline TermVariantList to_term_variant_list(const TermList& terms)
{
    auto result = TermVariantList {};
    result.reserve(terms.size());
    for (const auto& term : terms)
    {
        result.push_back(TermVariant(term));
    }
    return result;
}

inline FunctionExpressionVariantList to_function_expression_variant_list(const FunctionExpressionList& function_expressions)
{
    auto result = FunctionExpressionVariantList {};
    result.reserve(function_expressions.size());
    for (const auto& function_expression : function_expressions)
    {
        result.push_back(FunctionExpressionVariant(function_expression));
    }
    return result;
}

inline GroundFunctionExpressionVariantList to_ground_function_expression_variant_list(const GroundFunctionExpressionList& ground_function_expressions)
{
    auto result = GroundFunctionExpressionVariantList {};
    result.reserve(ground_function_expressions.size());
    for (const auto& function_expression : ground_function_expressions)
    {
        result.push_back(GroundFunctionExpressionVariant(function_expression));
    }
    return result;
}

}

#endif  // PYMIMIR_VARIANTS_HPP
