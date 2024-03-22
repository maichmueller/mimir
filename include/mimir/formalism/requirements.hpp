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

#ifndef MIMIR_FORMALISM_REQUIREMENTS_HPP_
#define MIMIR_FORMALISM_REQUIREMENTS_HPP_

#include "mimir/formalism/declarations.hpp"

#include <loki/pddl/factory.hpp>
#include <loki/pddl/requirements.hpp>
#include <set>
#include <string>
#include <unordered_map>

namespace mimir
{
class RequirementsImpl : public loki::Base<RequirementsImpl>
{
private:
    loki::pddl::RequirementEnumSet m_requirements;

    // Below: add additional members if needed and initialize them in the constructor

    RequirementsImpl(int identifier, loki::pddl::RequirementEnumSet requirements);

    // Give access to the constructor.
    friend class loki::PDDLFactory<RequirementsImpl, loki::Hash<RequirementsImpl*>, loki::EqualTo<RequirementsImpl*>>;

    /// @brief Test for semantic equivalence
    bool is_structurally_equivalent_to_impl(const RequirementsImpl& other) const;
    size_t hash_impl() const;

    // Give access to the private interface implementations.
    friend class loki::Base<RequirementsImpl>;

public:
    void str(std::ostream& out, const loki::FormattingOptions& options) const;

    bool test(loki::pddl::RequirementEnum requirement) const;

    const loki::pddl::RequirementEnumSet& get_requirements() const;
};
}

#endif
