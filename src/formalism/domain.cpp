/*
 * Copyright (C) 2023 Simon Stahlberg
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

#include <mimir/formalism/action_schema.hpp>
#include <mimir/formalism/domain.hpp>
#include <mimir/formalism/predicate.hpp>
#include <mimir/formalism/term.hpp>
#include <mimir/formalism/type.hpp>

#include <algorithm>
#include <loki/domain/parser.hpp>

namespace mimir
{

    Domain::Domain(loki::pddl::Domain external_domain) : external_(external_domain) {}

    Domain Domain::parse(const std::string& path)
    {
        loki::DomainParser parser(path);
        return Domain(parser.get_domain());
    }

    uint32_t Domain::get_id() const { return static_cast<uint32_t>(external_->get_identifier()); }

    const std::string& Domain::get_name() const { return external_->get_name(); }

    TypeList Domain::get_types() const { throw std::runtime_error("not implemented"); }

    TermList Domain::get_constants() const { throw std::runtime_error("not implemented"); }

    PredicateList Domain::get_predicates() const { throw std::runtime_error("not implemented"); }

    PredicateList Domain::get_static_predicates() const { throw std::runtime_error("not implemented"); }

    ActionSchemaList Domain::get_action_schemas() const { throw std::runtime_error("not implemented"); }

    std::map<std::string, Type> Domain::get_type_map() const { throw std::runtime_error("not implemented"); }

    std::map<std::string, Predicate> Domain::get_predicate_name_map() const { throw std::runtime_error("not implemented"); }

    std::map<std::string, Predicate> Domain::get_function_name_map() const { throw std::runtime_error("not implemented"); }

    std::map<uint32_t, Predicate> Domain::get_predicate_id_map() const { throw std::runtime_error("not implemented"); }

    std::map<std::string, Term> Domain::get_constant_map() const { throw std::runtime_error("not implemented"); }

}  // namespace mimir

/*
namespace std
{
    // Inject comparison and hash functions to make pointers behave appropriately with ordered and unordered datastructures
    std::size_t hash<mimir::Domain>::operator()(const mimir::Domain& domain) const
    {
        return hash_combine(domain.get_name(), domain.get_types(), domain.get_constants(), domain.get_predicates(), domain.get_action_schemas());
    }

    bool less<mimir::Domain>::operator()(const mimir::Domain& left_domain, const mimir::Domain& right_domain) const
    {
        return less_combine(
            std::make_tuple(left_domain.get_name(), left_domain.get_constants(), left_domain.get_predicates(), left_domain.get_action_schemas()),
            std::make_tuple(right_domain.get_name(), right_domain.get_constants(), right_domain.get_predicates(), right_domain.get_action_schemas()));
    }

    bool equal_to<mimir::Domain>::operator()(const mimir::Domain& left_domain, const mimir::Domain& right_domain) const
    {
        return equal_to_combine(
            std::make_tuple(left_domain.get_name(), left_domain.get_constants(), left_domain.get_predicates(), left_domain.get_action_schemas()),
            std::make_tuple(right_domain.get_name(), right_domain.get_constants(), right_domain.get_predicates(), right_domain.get_action_schemas()));
    }
}  // namespace std
*/
