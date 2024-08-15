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

#ifndef MIMIR_FORMALISM_OBJECT_HPP_
#define MIMIR_FORMALISM_OBJECT_HPP_

#include "mimir/formalism/declarations.hpp"

namespace mimir
{
/*
   TODO: Flattening ObjectImpl using a simple struct with the following fields
   1) Flat indices
   - uint64_t m_identifier; (8 byte)
   2) Data views
   - ConstView<String> m_name; (8 byte)
*/
class ObjectImpl
{
private:
    size_t m_index;
    std::string m_name;

    // Below: add additional members if needed and initialize them in the constructor

    ObjectImpl(size_t index, std::string name);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    std::string str() const;

    size_t get_index() const;
    const std::string& get_name() const;
};

extern std::ostream& operator<<(std::ostream& out, const ObjectImpl& element);

}

#endif
