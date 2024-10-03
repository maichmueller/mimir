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

#include "mimir/formalism/ground_atom.hpp"

#include "formatter.hpp"
#include "mimir/formalism/factories.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/predicate.hpp"
#include <map>

namespace mimir
{
template<PredicateCategory P>
GroundAtomImpl<P>::GroundAtomImpl(Index index, Predicate<P> predicate, ObjectList objects) :
    m_index(index),
    m_predicate(std::move(predicate)),
    m_objects(std::move(objects))
{
}

template<PredicateCategory P>
std::string GroundAtomImpl<P>::str() const
{
    auto out = std::stringstream();
    out << *this;
    return out.str();
}

template<PredicateCategory P>
Index GroundAtomImpl<P>::get_index() const
{
    return m_index;
}

template<PredicateCategory P>
Predicate<P> GroundAtomImpl<P>::get_predicate() const
{
    return m_predicate;
}

template<PredicateCategory P>
const ObjectList& GroundAtomImpl<P>::get_objects() const
{
    return m_objects;
}

template<PredicateCategory P>
size_t GroundAtomImpl<P>::get_arity() const
{
    return m_objects.size();
}

template<PredicateCategory P>
Atom<P> GroundAtomImpl<P>::lift(const TermList& terms, PDDLFactories& pddl_factories) const
{
    return pddl_factories.get_or_create_atom(m_predicate, terms);
}

template class GroundAtomImpl<Static>;
template class GroundAtomImpl<Fluent>;
template class GroundAtomImpl<Derived>;

template<PredicateCategory P>
std::pair<VariableList, AtomList<P>> lift(const GroundAtomList<P>& ground_atoms, PDDLFactories& pddl_factories)
{
    VariableList variables;
    AtomList<P> atoms;
    std::map<Object, Variable> to_variable;
    for (const auto& ground_atom : ground_atoms)
    {
        TermList terms;
        for (const auto& object : ground_atom->get_objects())
        {
            if (!to_variable.contains(object))
            {
                const auto parameter_index = to_variable.size();
                const auto variable_name = "x" + std::to_string(parameter_index);
                const auto variable = pddl_factories.get_or_create_variable(variable_name, parameter_index);
                variables.emplace_back(variable);
                to_variable.emplace(object, variable);
            }
            const auto variable = to_variable.at(object);
            const auto term = pddl_factories.get_or_create_term_variable(variable);
            terms.emplace_back(term);
        }
        atoms.emplace_back(ground_atom->lift(terms, pddl_factories));
    }
    return std::make_pair(variables, atoms);
}

template std::pair<VariableList, AtomList<Static>> lift(const GroundAtomList<Static>&, PDDLFactories&);
template std::pair<VariableList, AtomList<Fluent>> lift(const GroundAtomList<Fluent>&, PDDLFactories&);
template std::pair<VariableList, AtomList<Derived>> lift(const GroundAtomList<Derived>&, PDDLFactories&);

template<PredicateCategory P>
std::ostream& operator<<(std::ostream& out, const GroundAtomImpl<P>& element)
{
    auto formatter = PDDLFormatter();
    formatter.write(element, out);
    return out;
}

template std::ostream& operator<<(std::ostream& out, const GroundAtomImpl<Static>& element);
template std::ostream& operator<<(std::ostream& out, const GroundAtomImpl<Fluent>& element);
template std::ostream& operator<<(std::ostream& out, const GroundAtomImpl<Derived>& element);
}
