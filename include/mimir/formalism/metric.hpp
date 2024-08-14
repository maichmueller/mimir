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

#ifndef MIMIR_FORMALISM_METRIC_HPP_
#define MIMIR_FORMALISM_METRIC_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/equal_to.hpp"
#include "mimir/formalism/hash.hpp"

namespace mimir
{
class OptimizationMetricImpl
{
private:
    size_t m_index;
    loki::OptimizationMetricEnum m_optimization_metric;
    GroundFunctionExpression m_function_expression;

    // Below: add additional members if needed and initialize them in the constructor

    OptimizationMetricImpl(size_t index, loki::OptimizationMetricEnum optimization_metric, GroundFunctionExpression function_expression);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    size_t get_index() const;
    loki::OptimizationMetricEnum get_optimization_metric() const;
    const GroundFunctionExpression& get_function_expression() const;
};

template<>
struct UniquePDDLHasher<const OptimizationMetricImpl*>
{
    size_t operator()(const OptimizationMetricImpl* e) const;
};

template<>
struct UniquePDDLEqualTo<const OptimizationMetricImpl*>
{
    bool operator()(const OptimizationMetricImpl* l, const OptimizationMetricImpl* r) const;
};

extern std::ostream& operator<<(std::ostream& out, const OptimizationMetricImpl& element);

}

#endif
