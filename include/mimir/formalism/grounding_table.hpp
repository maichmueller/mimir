#pragma once

#include "mimir/common/hash.hpp"
#include "mimir/formalism/declarations.hpp"

#include <unordered_map>

namespace mimir
{

template<typename T>
using GroundingTable = unordered_map<ObjectList, T>;

template<typename T>
using GroundingTableList = vector<GroundingTable<T>>;

}
