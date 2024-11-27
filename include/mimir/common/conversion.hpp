#pragma once

#include "mimir/common/macros.hpp"
#include "mimir/common/type_traits.hpp"
#include "mimir/declarations.hpp"

#include <ranges>

namespace ranges
{

// RANGES_INLINE_VARIABLE(detail::to_container_fn<detail::from_range<mimir::vector>>, to_mimir_vector)
// RANGES_INLINE_VARIABLE(detail::to_container_fn<detail::from_range<mimir::unordered_set>>, to_mimir_set)

}

namespace mimir
{

template<class Type>
auto to_set(auto&& container)
{
    return std::invoke(
        [&]
        {
            Type value {};
            for (const auto& v : FWD(container))
            {
                value.insert(v);
            }
            return value;
        });
}

}
