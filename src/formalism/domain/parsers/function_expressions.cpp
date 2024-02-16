/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <mimir/formalism/domain/parsers/function_expressions.hpp>

#include <mimir/formalism/domain/parsers/function.hpp>

namespace mimir 
{
    FunctionExpressionVisitor::FunctionExpressionVisitor(PDDLFactories& factories_) : factories(factories_) {}

    FunctionExpression FunctionExpressionVisitor::operator()(const loki::pddl::FunctionExpressionNumberImpl& node) 
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return factories.function_expressions.get_or_create<FunctionExpressionNumberImpl>(
            node.get_number());
    }

    FunctionExpression FunctionExpressionVisitor::operator()(const loki::pddl::FunctionExpressionBinaryOperatorImpl& node)
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return factories.function_expressions.get_or_create<FunctionExpressionBinaryOperatorImpl>(
            node.get_binary_operator(), 
            parse(node.get_left_function_expression(), factories),
            parse(node.get_right_function_expression(), factories));
    }

    FunctionExpression FunctionExpressionVisitor::operator()(const loki::pddl::FunctionExpressionMultiOperatorImpl& node)
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return factories.function_expressions.get_or_create<FunctionExpressionMultiOperatorImpl>(
            node.get_multi_operator(), 
            parse(node.get_function_expressions(), factories));
    }

    FunctionExpression FunctionExpressionVisitor::operator()(const loki::pddl::FunctionExpressionMinusImpl& node)
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return factories.function_expressions.get_or_create<FunctionExpressionMinusImpl>(
            parse(node.get_function_expression(), factories));
    }

    FunctionExpression FunctionExpressionVisitor::operator()(const loki::pddl::FunctionExpressionFunctionImpl& node)
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return factories.function_expressions.get_or_create<FunctionExpressionFunctionImpl>(
            parse(node.get_function(), factories));
    }


    FunctionExpression parse(loki::pddl::FunctionExpression function_expression, PDDLFactories& factories)
    {
        std::cout << "FunctionExpression parse" << std::endl;
        return std::visit(FunctionExpressionVisitor(factories), *function_expression);
    }

    FunctionExpressionList parse(loki::pddl::FunctionExpressionList function_expression_list, PDDLFactories& factories)
    {
        std::cout << "FunctionExpressions parse" << std::endl;
        auto result_function_expression_list = FunctionExpressionList();
        for (const auto& function_expression : function_expression_list) {
            result_function_expression_list.push_back(parse(function_expression, factories));
        }
        return result_function_expression_list;
    }
}