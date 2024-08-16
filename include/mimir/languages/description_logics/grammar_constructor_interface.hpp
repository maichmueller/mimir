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
#include "mimir/languages/description_logics/constructor_interface.hpp"

#include <concepts>

namespace mimir::dl::grammar
{

/**
 * Grammar constructor hierarchy parallel to dl constructors.
 */

template<dl::IsConceptOrRole D>
class ConstructorImpl
{
protected:
    ConstructorImpl() = default;
    // Move constructor and move assignment operator are protected
    // to restrict their usage to derived classes only.
    ConstructorImpl(ConstructorImpl&& other) = default;
    ConstructorImpl& operator=(ConstructorImpl&& other) = default;

public:
    // Uncopieable
    ConstructorImpl(const ConstructorImpl& other) = delete;
    ConstructorImpl& operator=(const ConstructorImpl& other) = delete;

    virtual ~ConstructorImpl() = default;

    virtual bool test_match(dl::Constructor<D> constructor) const = 0;
};
}

#endif
