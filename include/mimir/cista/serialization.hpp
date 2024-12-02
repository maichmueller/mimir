#pragma once

#include "mimir/cista/containers/dynamic_bitset.h"
#include "mimir/common/macros.hpp"

#include <boost/hana.hpp>
#include <cista/serialization.h>

namespace cista
{

/// TODO:
///  Macro taken form cista/serialization.h (since it is #undef'ed there at the end)
///  Verify if this macro changes in future versions of cista and adapt this code accordingly
#ifndef cista_member_offset
#define cista_member_offset(Type, Member)                                      \
    (                                                                          \
        []()                                                                   \
        {                                                                      \
            if constexpr (std::is_standard_layout_v<Type>)                     \
            {                                                                  \
                return static_cast<::cista::offset_t>(offsetof(Type, Member)); \
            }                                                                  \
            else                                                               \
            {                                                                  \
                return ::cista::member_offset(null<Type>(), &Type::Member);    \
            }                                                                  \
        }())
#endif

template<typename Ctx, typename Block, template<typename> typename Ptr>
void serialize(Ctx& c, basic_dynamic_bitset<Block, Ptr> const* origin, offset_t const pos)
{
    using Type = basic_dynamic_bitset<Block, Ptr>;
    serialize(c, &origin->default_bit_value_, pos + cista_member_offset(Type, default_bit_value_));
    serialize(c, &origin->blocks_, pos + cista_member_offset(Type, blocks_));
}

// template<typename Ctx, typename... MapPairs>
// void serialize(Ctx& c, boost::hana::map<MapPairs...> const* origin, offset_t const pos)
//{
//     using Type = boost::hana::map<MapPairs...>;
//     boost::hana::for_each(boost::hana::keys(*origin), [&](auto key) {
//                               const auto& value = boost::hana::at_key(*origin, key);
//                               serialize(c, &value, pos);
//                           pos = pos + cista_member_offset()});
// }

#undef cista_member_offset

}  // namespace cista
