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

#pragma once


#include "mimir/formalism/declarations.hpp"
#include "mimir/languages/description_logics/grammar_constructor_repositories.hpp"

#include <loki/loki.hpp>

namespace mimir::dl::grammar
{

class Grammar
{
private:
    /* Memory */
    ConstructorTypeToRepository m_grammar_constructor_repos;

    /* The rules of the grammar. */
    DerivationRuleList<Concept> m_concept_rules;
    DerivationRuleList<Role> m_role_rules;

public:
    /// @brief Create a grammar from a BNF description.
    Grammar(std::string bnf_description, Domain domain);

    /// @brief Tests whether a dl concept constructor satisfies the grammar specification.
    /// @param constructor is the dl concept constructor to test.
    /// @return true iff the dl concept constructor satisfies the grammar specification, and false otherwise.
    bool test_match(dl::Constructor<Concept> constructor) const;

    /// @brief Tests whether a dl role constructor satisfies the grammar specfication.
    /// @param constructor is the dl role constructor to test.
    /// @return true iff the dl concept constructor satisfies the grammar specification, and false otherwise.
    bool test_match(dl::Constructor<Role> constructor) const;

    /**
     * Getters
     */
    const DerivationRuleList<Concept>& get_concept_rules() const;
    const DerivationRuleList<Role>& get_role_rules() const;
};
}


