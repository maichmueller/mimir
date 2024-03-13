/*
 * Copyright (C) 2023 Dominik Drexler
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

#ifndef MIMIR_FORMALISM_TERM_HPP_
#define MIMIR_FORMALISM_TERM_HPP_

#include "mimir/formalism/declarations.hpp"

#include <loki/pddl/term.hpp>
#include <string>

namespace loki
{
template<typename HolderType>
class PersistentFactory;
}

namespace mimir
{
class TermObjectImpl : public loki::Base<TermObjectImpl>
{
private:
    Object m_object;

    // Below: add additional members if needed and initialize them in the constructor

    TermObjectImpl(int identifier, Object object);

    // Give access to the constructor.
    template<typename>
    friend class loki::PersistentFactory;

    bool is_structurally_equivalent_to_impl(const TermObjectImpl& other) const;
    size_t hash_impl() const;
    void str_impl(std::ostringstream& out, const loki::FormattingOptions& options) const;

    // Give access to the private interface implementations.
    friend class loki::Base<TermObjectImpl>;

public:
    const Object& get_object() const;
};

class TermVariableImpl : public loki::Base<TermVariableImpl>
{
private:
    Variable m_variable;

    TermVariableImpl(int identifier, Variable variable);

    // Give access to the constructor.
    template<typename>
    friend class loki::PersistentFactory;

    bool is_structurally_equivalent_to_impl(const TermVariableImpl& other) const;
    size_t hash_impl() const;
    void str_impl(std::ostringstream& out, const loki::FormattingOptions& options) const;

    // Give access to the private interface implementations.
    friend class loki::Base<TermVariableImpl>;

public:
    const Variable& get_variable() const;
};
}

namespace std
{
// Inject comparison and hash function to make pointers behave appropriately with ordered and unordered datastructures
template<>
struct less<mimir::Term>
{
    bool operator()(const mimir::Term& left_term, const mimir::Term& right_term) const;
};

template<>
struct hash<mimir::TermObjectImpl>
{
    std::size_t operator()(const mimir::TermObjectImpl& term) const;
};

template<>
struct hash<mimir::TermVariableImpl>
{
    std::size_t operator()(const mimir::TermVariableImpl& term) const;
};
}

#endif