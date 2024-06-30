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

#ifndef MIMIR_LANGUAGES_DESCRIPTION_LOGICS_GRAMMAR_CONSTRUCTORS_INTERFACE_HPP_
#define MIMIR_LANGUAGES_DESCRIPTION_LOGICS_GRAMMAR_CONSTRUCTORS_INTERFACE_HPP_

#include "mimir/formalism/predicate.hpp"
#include "mimir/languages/description_logics/constructors_interface.hpp"

#include <concepts>

namespace mimir::dl::grammar
{

/**
 * Grammar constructor hierarchy parallel to dl constructors.
 */

template<dl::IsConceptOrRole D>
class Constructor
{
protected:
    bool type_equal(const Constructor& other) const { return typeid(*this) == typeid(other); }

public:
    virtual ~Constructor() = default;

    bool operator==(const Constructor& other) const { return is_equal(other); }

    virtual bool is_equal(const Constructor& other) const = 0;
    virtual size_t hash() const = 0;

    virtual bool test_match(const D& constructor) const = 0;

    virtual size_t get_id() const = 0;
};

using Concept = Constructor<dl::Concept>;
using Role = Constructor<dl::Role>;

}

#endif
