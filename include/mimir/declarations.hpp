#pragma once

#include <ankerl/cista_adapter.h>
#include <cista/containers.h>

namespace mimir
{

using string = cista::offset::string;
using string_view = cista::offset::string_view;

template<typename T>
using vector = cista::offset::vector<T>;

template<class Key,  //
         class T,
         class Hash = cista::hash_all,
         class KeyEqual = cista::equals_all,
         class AllocatorOrContainer = vector<std::pair<Key, T>>>
using unordered_map = cista::offset::ankerl_map<Key, T, Hash, AllocatorOrContainer>;

template<class Key,  //
         class T,
         class Hash = cista::hash_all,
         class KeyEqual = cista::equals_all,
         class AllocatorOrContainer = vector<std::pair<Key, T>>>
using unordered_set = cista::offset::ankerl_set<Key, Hash, AllocatorOrContainer>;

}