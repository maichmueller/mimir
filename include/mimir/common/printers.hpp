
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
 *<
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MIMIR_COMMON_PRINTERS_HPP_
#define MIMIR_COMMON_PRINTERS_HPP_

#include "mimir/common/translations.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/actions/dense.hpp"
#include "mimir/search/axioms/dense.hpp"
#include "mimir/search/states/dense.hpp"

#include <ostream>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace mimir
{

/// @brief Prints a State to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<DenseState, const PDDLFactories&>& data);

/// @brief Prints an Action to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<DenseAction, const PDDLFactories&>& data);

/// @brief Prints an Axiom to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<DenseAxiom, const PDDLFactories&>& data);

std::ostream& operator<<(std::ostream& os, const DenseAction& action);

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec)
{
    os << "[";
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        os << vec[i];
    }
    os << "]";
    return os;
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::unordered_set<T>& set)
{
    os << "{";
    size_t i = 0;
    for (const auto& element : set)
    {
        if (i != 0)
            os << ", ";
        os << element;
        ++i;
    }
    os << "}";
    return os;
}

}

#endif
