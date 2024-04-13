/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg and Simon Stahlberg
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

#include "numeric_fluent.hpp"

#include "function.hpp"
#include "ground_atom.hpp"
#include "mimir/formalism/factories.hpp"

namespace mimir
{
NumericFluent parse(loki::NumericFluent numeric_fluent, PDDLFactories& factories)
{
    return factories.get_or_create_numeric_fluent(parse(numeric_fluent->get_function(), factories), numeric_fluent->get_number());
}

NumericFluentList parse(loki::NumericFluentList numeric_fluent_list, PDDLFactories& factories)
{
    auto result_numeric_fluent_list = NumericFluentList();
    for (const auto& numeric_fluent : numeric_fluent_list)
    {
        result_numeric_fluent_list.push_back(parse(numeric_fluent, factories));
    }
    return result_numeric_fluent_list;
}
}
