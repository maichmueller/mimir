
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
#include "mimir/search/actions.hpp"
#include "mimir/search/states.hpp"

#include <ostream>
#include <tuple>
#include <vector>

namespace mimir
{

/// @brief Prints a State to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<ConstView<StateDispatcher<DenseStateTag>>, const PDDLFactories&>& data);

/// @brief Prints an Action to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<ConstView<ActionDispatcher<DenseStateTag>>, const PDDLFactories&>& data);

std::ostream& operator<<(std::ostream& os, const DenseAction& action);

}

#endif
