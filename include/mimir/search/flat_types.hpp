#ifndef MIMIR_SEARCH_FLAT_TYPES_HPP_
#define MIMIR_SEARCH_FLAT_TYPES_HPP_

#include "mimir/formalism/declarations.hpp"

#include <flatmemory/flatmemory.hpp>

namespace mimir
{
/* Bitset */

using FlatBitsetLayout = flatmemory::Bitset<uint64_t>;
using FlatBitsetBuilder = flatmemory::Builder<FlatBitsetLayout>;
using FlatBitset = flatmemory::ConstView<FlatBitsetLayout>;

using FlatBitsetVectorLayout = flatmemory::Vector<flatmemory::Bitset<uint64_t>>;
using FlatBitsetVectorBuilder = flatmemory::Builder<FlatBitsetVectorLayout>;
using FlatBitsetVector = flatmemory::ConstView<FlatBitsetVectorLayout>;

/* ObjectList */

using FlatObjectListLayout = flatmemory::Vector<Object>;
using FlatObjectListBuilder = flatmemory::Builder<FlatObjectListLayout>;
using FlatObjectList = flatmemory::ConstView<FlatObjectListLayout>;
}

#endif
