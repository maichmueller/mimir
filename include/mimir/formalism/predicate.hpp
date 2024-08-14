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

#ifndef MIMIR_FORMALISM_PREDICATE_HPP_
#define MIMIR_FORMALISM_PREDICATE_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/equal_to.hpp"
#include "mimir/formalism/hash.hpp"

namespace mimir
{

/*
   TODO: Flattening PredicateImpl using a tuple with the following fields
   1) Flat indices
   - uint64_t m_identifier; (8 byte)
   - Vector<uint64_t> m_variable_ids; (variable sized)
   2) Data views
   - ConstView<String> m_name; (8 byte)
   - ConstView<Vector<Variable>> m_variables (8 byte)
*/
template<PredicateCategory P>
class PredicateImpl
{
private:
    size_t m_index;
    std::string m_name;
    VariableList m_parameters;

    // Below: add additional members if needed and initialize them in the constructor

    PredicateImpl(size_t index, std::string name, VariableList parameters);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    using Category = P;

    size_t get_index() const;
    const std::string& get_name() const;
    const VariableList& get_parameters() const;
    size_t get_arity() const;
};

template<PredicateCategory P>
struct UniquePDDLHasher<const PredicateImpl<P>*>
{
    size_t operator()(const PredicateImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniquePDDLEqualTo<const PredicateImpl<P>*>
{
    bool operator()(const PredicateImpl<P>* l, const PredicateImpl<P>* r) const;
};

template<PredicateCategory P>
extern std::ostream& operator<<(std::ostream& out, const PredicateImpl<P>& element);

}

#endif
