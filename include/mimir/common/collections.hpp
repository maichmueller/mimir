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

#pragma once

#include "mimir/declarations.hpp"

#include <boost/container/small_vector.hpp>
#include <range/v3/to_container.hpp>
#include <unordered_set>
#include <vector>

namespace mimir
{

/// @brief A wrapper around std::includes.
/// @tparam T the vector value type.
/// @param vec1 the first vector.
/// @param vec2 the second vector.
/// @return true iff the first vector is a subset or equal to the second vector, and false otherwise.
template<typename T>
bool is_subseteq(const vector<T>& vec1, const vector<T>& vec2)
{
    assert(std::is_sorted(vec1.begin(), vec1.end()));
    assert(std::is_sorted(vec2.begin(), vec2.end()));

    return std::includes(vec2.begin(), vec2.end(), vec1.begin(), vec1.end());
}

/// @brief A version of std::set_intersection that does not produce the intersection.
/// @tparam T the vector value type.
/// @param vec1 the first vector.
/// @param vec2 the second vector.
/// @return true iff first and second vector are disjoint, i.e., do not share a common element, and false otherwise.
template<typename T>
bool are_disjoint(const vector<T>& vec1, const vector<T>& vec2)
{
    assert(std::is_sorted(vec1.begin(), vec1.end()));
    assert(std::is_sorted(vec2.begin(), vec2.end()));

    // Use two iterators to traverse both vectors simultaneously
    auto it1 = vec1.begin();
    auto it2 = vec2.begin();

    while (it1 != vec1.end() && it2 != vec2.end())
    {
        if (*it1 < *it2)
        {
            ++it1;
        }
        else if (*it2 < *it1)
        {
            ++it2;
        }
        else
        {
            // Common element found
            return false;
        }
    }

    // No common element found
    return true;
}

/// @brief Check whether all elements in the vector are unique.
/// @tparam T the vector value type.
/// @param vec the vector.
/// @return true iff all element in the vector are unique, i.e., do not occur more than once, and false otherwise.
template<std::ranges::range Range>
extern bool is_all_unique(const Range& rng)
{
    using T = std::ranges::range_value_t<Range>;
    mimir::unordered_set<T> set {};
    return std::ranges::all_of(rng, [&](const auto elem) { return set.emplace(elem).second; });
}

/// @brief Uniquify elements in a vector of elements.
template<std::ranges::range Range>
extern mimir::unordered_set<std::ranges::range_value_t<Range>> uniquify_elements(const Range& rng)
{
    using T = std::ranges::range_value_t<Range>;
    mimir::unordered_set<T> set {};
    // manual insertion is faster than constructor method (at least for std::unordered_set)
    for (const auto elem : rng)
    {
        set.emplace(elem);
    }
    return set;
}

}
