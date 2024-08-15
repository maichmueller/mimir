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
 *<
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MIMIR_FORMALISM_DOMAIN_HPP_
#define MIMIR_FORMALISM_DOMAIN_HPP_

#include "mimir/formalism/declarations.hpp"

namespace mimir
{
class DomainImpl
{
private:
    size_t m_index;
    std::optional<fs::path> m_filepath;
    std::string m_name;
    Requirements m_requirements;
    ObjectList m_constants;
    PredicateList<Static> m_static_predicates;
    PredicateList<Fluent> m_fluent_predicates;
    PredicateList<Derived> m_derived_predicates;
    FunctionSkeletonList m_functions;
    ActionList m_actions;
    AxiomList m_axioms;

    // Below: add additional members if needed and initialize them in the constructor
    ToPredicateMap<std::string, Static> m_name_to_static_predicate;
    ToPredicateMap<std::string, Fluent> m_name_to_fluent_predicate;
    ToPredicateMap<std::string, Derived> m_name_to_derived_predicate;

    DomainImpl(size_t index,
               std::optional<fs::path> filepath,
               std::string name,
               Requirements requirements,
               ObjectList constants,
               PredicateList<Static> static_predicates,
               PredicateList<Fluent> fluent_predicates,
               PredicateList<Derived> derived_predicates,
               FunctionSkeletonList functions,
               ActionList actions,
               AxiomList axioms);

    // Give access to the constructor.
    template<typename HolderType, typename Hash, typename EqualTo>
    friend class loki::UniqueFactory;

public:
    size_t get_index() const;
    const std::optional<fs::path>& get_filepath() const;
    const std::string& get_name() const;
    const Requirements& get_requirements() const;
    const ObjectList& get_constants() const;
    template<PredicateCategory P>
    const PredicateList<P>& get_predicates() const;
    const FunctionSkeletonList& get_functions() const;
    const ActionList& get_actions() const;
    const AxiomList& get_axioms() const;

    template<PredicateCategory P>
    const ToPredicateMap<std::string, P>& get_name_to_predicate() const;
};

extern std::ostream& operator<<(std::ostream& out, const DomainImpl& element);

}

#endif
