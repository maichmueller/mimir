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

#ifndef MIMIR_FORMALISM_AXIOM_HPP_
#define MIMIR_FORMALISM_AXIOM_HPP_

#include "mimir/formalism/literal.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/variable.hpp"

#include <loki/loki.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace mimir
{
class AxiomImpl : public loki::Base<AxiomImpl>
{
private:
    VariableList m_parameters;
    Literal<Fluent> m_literal;
    LiteralList<Static> m_static_conditions;
    LiteralList<Fluent> m_fluent_conditions;

    AxiomImpl(int identifier, VariableList parameters, Literal<Fluent> literal, LiteralList<Static> static_conditions, LiteralList<Fluent> fluent_conditions);

    // Give access to the constructor.
    friend class loki::PDDLFactory<AxiomImpl, loki::Hash<AxiomImpl*>, loki::EqualTo<AxiomImpl*>>;

    /// @brief Test for structural equivalence
    bool is_structurally_equivalent_to_impl(const AxiomImpl& other) const;
    size_t hash_impl() const;
    void str_impl(std::ostream& out, const loki::FormattingOptions& options) const;

    // Give access to the private interface implementations.
    friend class loki::Base<AxiomImpl>;

public:
    const VariableList& get_parameters() const;
    const Literal<Fluent>& get_literal() const;
    const LiteralList<Static>& get_static_conditions() const;
    const LiteralList<Fluent>& get_fluent_conditions() const;

    size_t get_arity() const;
};

/**
 * Type aliases
 */

using Axiom = const AxiomImpl*;
using AxiomList = std::vector<Axiom>;
using AxiomSet = std::unordered_set<Axiom>;

}

#endif
