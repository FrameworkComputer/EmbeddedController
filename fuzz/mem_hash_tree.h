// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __FUZZ_MEM_HASH_TREE_H
#define __FUZZ_MEM_HASH_TREE_H
#include <unistd.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#define HIDE_EC_STDLIB
#include "board/host/dcrypto.h"
#include "fuzz/span.h"

// MaskedLabel.first is the label path, this is shifted to the right by the
//   (bits_per_level * level)
// MaskedLabel.second is the level of the label (0 for leaf, height for root)
typedef std::pair<uint64_t, uint8_t> MaskedLabel;

namespace std {
template <>
struct hash<MaskedLabel> {
  size_t operator()(const MaskedLabel& lbl) const {
    static const auto hash_first = hash<uint64_t>();
    static const auto hash_second = hash<uint8_t>();
    return hash_first(lbl.first) * hash_second(lbl.second);
  }
};
}  // namespace std

class MemHashTree {
 public:
  MemHashTree();

  bool GetLeaf(uint64_t label, fuzz::span<uint8_t> leaf_hash) const;
  // Writes the result to |path_hashes| and returns the size in bytes of the
  // returned path for use in serializers that report how much buffer was used.
  size_t GetPath(uint64_t label, fuzz::span<uint8_t> path_hashes) const;
  // Updates the hashes in the path of the specified leaf. If |path_hash| is
  // empty, the entry in hash_tree_ is deleted representing an empty leaf.
  void UpdatePath(uint64_t label, fuzz::span<const uint8_t> path_hash);

  void Reset();
  void Reset(uint8_t bits_per_level, uint8_t height);

 private:
  uint8_t bits_per_level_;
  uint8_t height_;

  // Only contains hashes for non empty paths in the tree.
  std::unordered_map<MaskedLabel, std::array<uint8_t, 32>> hash_tree_;
  std::vector<std::array<uint8_t, 32>> empty_node_hashes_;
};

#endif  // __FUZZ_MEM_HASH_TREE_H
