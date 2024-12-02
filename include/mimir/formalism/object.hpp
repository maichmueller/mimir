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

#pragma once

#include "mimir/formalism/declarations.hpp"

namespace mimir
{

struct ObjectImpl
{
    Index m_index;
    mimir::string m_name;

    std::string str() const;

    Index get_index() const;
    std::string_view get_name() const;
};

extern std::ostream& operator<<(std::ostream& out, const ObjectImpl& element);

extern std::ostream& operator<<(std::ostream& out, Object element);

}

#include "mimir/common/macros.hpp"

#include <fmt/ostream.h>
FORMATTABLE(mimir::ObjectImpl);
FORMATTABLE(mimir::Object);