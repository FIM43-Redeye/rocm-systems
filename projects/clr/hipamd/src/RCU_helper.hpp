/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCU_HELPER_HPP_
#define RCU_HELPER_HPP_

#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "thread/monitor.hpp"

namespace hip {

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEq = std::equal_to<K>>
class RCUUnorderedMap {
 public:
  using MapType = std::unordered_map<K, V, Hash, KeyEq>;
  using Snapshot = std::shared_ptr<const MapType>;

  RCUUnorderedMap() : snapshot_(std::make_shared<MapType>()) {}

  Snapshot snapshot() const {
    return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  }

  bool contains(const K& key) const {
    auto snap = snapshot();
    return snap->find(key) != snap->end();
  }

  std::optional<V> find_copy(const K& key) const {
    auto snap = snapshot();
    auto it = snap->find(key);
    if (it == snap->end()) {
      return std::nullopt;
    }
    return it->second;
  }

  template <typename Fn>
  bool with_value(const K& key, Fn&& fn) const {
    auto snap = snapshot();
    auto it = snap->find(key);
    if (it == snap->end()) {
      return false;
    }
    fn(it->second);
    return true;
  }

  template <typename Fn>
  void update(Fn&& fn) {
    std::lock_guard lock(writer_mutex_.get());
    auto current = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    auto updated = std::make_shared<MapType>(*current);
    fn(*updated);
    Snapshot published(updated);
    std::atomic_store_explicit(&snapshot_, published, std::memory_order_release);
  }

  void insert_or_assign(const K& key, const V& value) {
    update([&](MapType& map) { map.insert_or_assign(key, value); });
  }

  bool erase(const K& key) {
    bool erased = false;
    update([&](MapType& map) { erased = (map.erase(key) > 0); });
    return erased;
  }

  void clear() { update([](MapType& map) { map.clear(); }); }

  size_t size() const { return snapshot()->size(); }

 private:
  mutable amd::padded_mutex writer_mutex_;
  std::shared_ptr<const MapType> snapshot_;
};

}  // namespace hip

#endif  // RCU_HELPER_HPP_
