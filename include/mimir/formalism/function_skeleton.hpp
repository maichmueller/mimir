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

#ifndef MIMIR_FORMALISM_FUNCTION_SKELETON_HPP_
#define MIMIR_FORMALISM_FUNCTION_SKELETON_HPP_

#include "mimir/formalism/declarations.hpp"

namespace mimir
{
class FunctionSkeletonImpl
{
private:
    size_t m_index;
    std::string m_name;
    VariableList m_parameters;

    // Below: add additional members if needed and initialize them in the constructor

    FunctionSkeletonImpl(size_t index, std::string name, VariableList parameters);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    size_t get_index() const;
    const std::string& get_name() const;
    const VariableList& get_parameters() const;
};

extern std::ostream& operator<<(std::ostream& out, const FunctionSkeletonImpl& element);

}

#endif
