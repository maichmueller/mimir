#pragma once

#include <ankerl/cista_adapter.h>
#include <ankerl/unordered_dense.h>
#include <cista/containers.h>
#include <cista/type_hash/static_type_hash.h>

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
         class AllocatorOrContainer = vector<std::pair<Key, std::unique_ptr<T>>>>
using node_hash_map = cista::offset::ankerl_map<Key, std::unique_ptr<T>, Hash, KeyEqual, AllocatorOrContainer>;

// cista ankerl::unordered_dense::map
template<class Key,  //
         class T,
         class Hash = cista::hash_all,
         class KeyEqual = cista::equals_all,
         class AllocatorOrContainer = vector<std::pair<Key, T>>>
using unordered_map = cista::offset::ankerl_map<Key, T, Hash, KeyEqual, AllocatorOrContainer>;

// vanilla ankerl::unordered_dense::map
// template<class Key,
//         class T,
//         class Hash = ankerl::unordered_dense::hash<Key>,
//         class KeyEqual = std::equal_to<Key>,
//         class AllocatorOrContainer = std::allocator<std::pair<Key, T>>>
// using unordered_map = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual, AllocatorOrContainer>;

// cista ankerl::unordered_dense::set
template<class Key,  //
         class Hash = cista::hash_all,
         class KeyEqual = cista::equals_all,
         class AllocatorOrContainer = cista::offset::vector<Key>>
using unordered_set = cista::offset::ankerl_set<Key, Hash, KeyEqual, AllocatorOrContainer>;

// vanilla ankerl::unordered_dense::set
// template<class Key,  //
//         class Hash = cista::hash_all,
//         class KeyEqual = cista::equals_all,
//         class AllocatorOrContainer = vector<Key>>
// using unordered_set = cista::offset::ankerl_set<Key, Hash, KeyEqual, AllocatorOrContainer>;

template<typename T>
using hash = cista::hashing<T>;

template<typename T>
using equal_to = cista::equal_to<T>;

}