// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzz/mem_hash_tree.h"

#include <algorithm>
#include <cassert>

MemHashTree::MemHashTree() : bits_per_level_(0), height_(0) {}

bool MemHashTree::GetLeaf(uint64_t label, fuzz::span<uint8_t> leaf_hash) const {
  assert(leaf_hash.size() >= SHA256_DIGEST_SIZE);
  auto itr = hash_tree_.find(MaskedLabel(label, 0));
  if (itr == hash_tree_.end()) {
    std::fill(leaf_hash.begin(), leaf_hash.end(), 0);
    return false;
  }

  std::copy(itr->second.begin(), itr->second.end(), leaf_hash.begin());
  return true;
}

size_t MemHashTree::GetPath(uint64_t label,
                            fuzz::span<uint8_t> path_hashes) const {
  uint8_t fan_out = 1 << bits_per_level_;
  uint8_t num_siblings = fan_out - 1;
  assert(path_hashes.size() >= num_siblings * height_ * SHA256_DIGEST_SIZE);
  // num_siblings and child_index_mask have the same value, but were named
  // differently to help convey how they are used.
  uint64_t child_index_mask = fan_out - 1;
  uint64_t shifted_parent_label = label;
  uint8_t* dest_itr = path_hashes.begin();
  for (uint8_t level = 0; level < height_; ++level) {
    uint8_t label_index = shifted_parent_label & child_index_mask;
    shifted_parent_label &= ~child_index_mask;
    for (uint8_t index = 0; index < fan_out; ++index) {
      // Only include hashes for sibling nodes.
      if (index == label_index) {
        continue;
      }
      auto src_itr =
          hash_tree_.find(MaskedLabel(shifted_parent_label | index, level));
      if (src_itr == hash_tree_.end()) {
        std::copy(empty_node_hashes_[level].begin(),
                  empty_node_hashes_[level].end(), dest_itr);
      } else {
        std::copy(src_itr->second.begin(), src_itr->second.end(), dest_itr);
      }
      dest_itr += SHA256_DIGEST_SIZE;
    }
    shifted_parent_label = shifted_parent_label >> bits_per_level_;
  }
  return dest_itr - path_hashes.begin();
}

void MemHashTree::UpdatePath(uint64_t label,
                             fuzz::span<const uint8_t> path_hash) {
  std::array<uint8_t, SHA256_DIGEST_SIZE> hash;
  if (path_hash.empty()) {
    std::fill(hash.begin(), hash.end(), 0);
    hash_tree_.erase(MaskedLabel(label, 0));
  } else {
    assert(path_hash.size() == SHA256_DIGEST_SIZE);
    std::copy(path_hash.begin(), path_hash.end(), hash.begin());
    hash_tree_[MaskedLabel(label, 0)] = hash;
  }

  uint8_t fan_out = 1 << bits_per_level_;
  uint64_t child_index_mask = fan_out - 1;
  uint64_t shifted_parent_label = label;
  for (int level = 0; level < height_; ++level) {
    shifted_parent_label &= ~child_index_mask;

    LITE_SHA256_CTX ctx;
    DCRYPTO_SHA256_init(&ctx, 1);
    int empty_nodes = 0;
    for (int index = 0; index < fan_out; ++index) {
      auto itr =
          hash_tree_.find(MaskedLabel(shifted_parent_label | index, level));
      if (itr == hash_tree_.end()) {
        HASH_update(&ctx, empty_node_hashes_[level].data(),
                    empty_node_hashes_[level].size());
        ++empty_nodes;
      } else {
        HASH_update(&ctx, itr->second.data(), itr->second.size());
      }
    }
    shifted_parent_label = shifted_parent_label >> bits_per_level_;

    const uint8_t* temp = HASH_final(&ctx);
    std::copy(temp, temp + SHA256_DIGEST_SIZE, hash.begin());
    MaskedLabel node_key(shifted_parent_label, level + 1);
    if (empty_nodes == fan_out) {
      hash_tree_.erase(node_key);
    } else {
      hash_tree_[node_key] = hash;
    }
  }
}

void MemHashTree::Reset() {
  bits_per_level_ = 0;
  height_ = 0;
  empty_node_hashes_.clear();
  hash_tree_.clear();
}

void MemHashTree::Reset(uint8_t bits_per_level, uint8_t height) {
  bits_per_level_ = bits_per_level;
  height_ = height;
  hash_tree_.clear();
  empty_node_hashes_.resize(height);

  std::array<uint8_t, SHA256_DIGEST_SIZE> hash;
  std::fill(hash.begin(), hash.end(), 0);
  empty_node_hashes_[0] = hash;

  uint8_t fan_out = 1 << bits_per_level;
  for (int level = 1; level < height; ++level) {
    LITE_SHA256_CTX ctx;
    DCRYPTO_SHA256_init(&ctx, 1);
    for (int index = 0; index < fan_out; ++index) {
      HASH_update(&ctx, hash.data(), hash.size());
    }
    const uint8_t* temp = HASH_final(&ctx);
    std::copy(temp, temp + SHA256_DIGEST_SIZE, hash.begin());
    empty_node_hashes_[level] = hash;
  }
}
