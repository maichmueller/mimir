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

#ifndef MIMIR_FORMALISM_FUNCTION_SKELETON_HPP_
#define MIMIR_FORMALISM_FUNCTION_SKELETON_HPP_

#include "mimir/formalism/declarations.hpp"

#include <loki/pddl/factory.hpp>
#include <loki/pddl/function_skeleton.hpp>
#include <string>

namespace mimir
{
class FunctionSkeletonImpl : public loki::Base<FunctionSkeletonImpl>
{
private:
    std::string m_name;
    ParameterList m_parameters;
    Type m_type;

    // Below: add additional members if needed and initialize them in the constructor

    FunctionSkeletonImpl(int identifier, std::string name, ParameterList parameters, Type type);

    // Give access to the constructor.
    friend class loki::PDDLFactory<FunctionSkeletonImpl, loki::Hash<FunctionSkeletonImpl*>, loki::EqualTo<FunctionSkeletonImpl*>>;

    /// @brief Test for semantic equivalence
    bool is_structurally_equivalent_to_impl(const FunctionSkeletonImpl& other) const;
    size_t hash_impl() const;

    // Give access to the private interface implementations.
    friend class loki::Base<FunctionSkeletonImpl>;

public:
    void str(std::ostream& out, const loki::FormattingOptions& options, bool typing_enabled) const;

    const std::string& get_name() const;
    const ParameterList& get_parameters() const;
    const Type& get_type() const;
};
}

#endif
