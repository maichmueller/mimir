#pragma once

#include "mimir/common/hash.hpp"
#include "mimir/formalism/declarations.hpp"

#include <unordered_map>

namespace mimir
{

template<typename T>
using GroundingTable = std::unordered_map<ObjectList, T, Hash<ObjectList>>;

template<typename T>
using GroundingTableList = vector<GroundingTable<T>>;

}
