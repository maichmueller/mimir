#pragma once

#include "mimir/common/deref.hpp"
#include "mimir/common/macros.hpp"
#include "mimir/common/type_traits.hpp"

#include <range/v3/all.hpp>

namespace ranges::views
{

constexpr auto deref = std::views::transform(mimir::dereffer {});

template<typename T>
constexpr auto cast = std::views::transform([](auto&& value) { return static_cast<T>(FWD(value)); });

}  // namespace ranges::views
