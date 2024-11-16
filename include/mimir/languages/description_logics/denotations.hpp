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

#include "mimir/cista/storage/unordered_set.h"
#include "mimir/common/equal_to.hpp"
#include "mimir/common/hash.hpp"
#include "mimir/common/hash_cista.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/languages/description_logics/constructor_category.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mimir::dl
{

template<IsConceptOrRole D>
struct DenotationImpl
{
    using DenotationType = typename D::DenotationType;

    DenotationType m_data = DenotationType();

    DenotationType& get_data() { return m_data; }
    const DenotationType& get_data() const { return m_data; }
};

}

template<mimir::dl::IsConceptOrRole D>
struct cista::storage::DerefStdHasher<mimir::dl::DenotationImpl<D>>
{
    size_t operator()(const mimir::dl::DenotationImpl<D>* ptr) const { return mimir::hash_combine(ptr->get_data()); }
};
template<mimir::dl::IsConceptOrRole D>
struct cista::storage::DerefStdEqualTo<mimir::dl::DenotationImpl<D>>
{
    bool operator()(const mimir::dl::DenotationImpl<D>* lhs, const mimir::dl::DenotationImpl<D>* rhs) const { return lhs->get_data() == rhs->get_data(); }
};

namespace mimir::dl
{

template<IsConceptOrRole D>
using DenotationImplSet = cista::storage::UnorderedSet<DenotationImpl<D>>;

}


