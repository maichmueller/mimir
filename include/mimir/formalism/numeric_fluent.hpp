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

#ifndef MIMIR_FORMALISM_NUMERIC_FLUENT_HPP_
#define MIMIR_FORMALISM_NUMERIC_FLUENT_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/equal_to.hpp"
#include "mimir/formalism/hash.hpp"

namespace mimir
{
class NumericFluentImpl
{
private:
    size_t m_index;
    GroundFunction m_function;
    double m_number;

    // Below: add additional members if needed and initialize them in the constructor

    NumericFluentImpl(size_t index, GroundFunction function, double number);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    size_t get_index() const;
    const GroundFunction& get_function() const;
    double get_number() const;
};

template<>
struct UniquePDDLHasher<const NumericFluentImpl*>
{
    size_t operator()(const NumericFluentImpl* e) const;
};

template<>
struct UniquePDDLEqualTo<const NumericFluentImpl*>
{
    bool operator()(const NumericFluentImpl* l, const NumericFluentImpl* r) const;
};

extern std::ostream& operator<<(std::ostream& out, const NumericFluentImpl& element);

}

#endif
