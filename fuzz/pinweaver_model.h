// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pinweaver specific model to facilitate fuzzing.

#ifndef __FUZZ_PINWEAVER_MODEL_H
#define __FUZZ_PINWEAVER_MODEL_H

#include <deque>
#include <memory>
#include <unordered_map>

#define HIDE_EC_STDLIB
#include "fuzz/cr50_fuzz.pb.h"
#include "fuzz/mem_hash_tree.h"
#include "fuzz/span.h"
#include "include/pinweaver.h"
#include "include/pinweaver_types.h"

// Provides enough state tracking to send valid PinWeaver requests. This is
// necessary because of the authentication dependent fields used by the Merkle
// tree such as HMACs and a set of sibling path hashes that must be correct to
// reach some parts of the PinWeaver code.
class PinweaverModel {
 public:
  PinweaverModel();

  void SendBuffer(fuzz::span<uint8_t> buffer);

  // Converts the logical representation of a request used in fuzzing into bytes
  // that can be processed by the pinweaver code for fuzzing.
  size_t SerializeRequest(const fuzz::pinweaver::Request& pinweaver,
                          fuzz::span<uint8_t> buffer) const;

  // Executes a request in the form of a  fuzz::pinweaver::Request proto, and
  // updates the model, so that future requests will be valid.
  uint32_t ApplyRequest(const fuzz::pinweaver::Request& pinweaver,
                        fuzz::span<uint8_t> buffer);

  // Clears any state. This shoudl be called at the beginning of each fuzzing
  // iteration.
  void Reset();

 private:
  static constexpr uint8_t kNullRootHash[PW_HASH_SIZE] = {};

  struct LeafData {
    std::vector<uint8_t> wrapped_data;
    std::array<uint8_t, PW_SECRET_SIZE> low_entropy_secret;
    std::array<uint8_t, PW_SECRET_SIZE> reset_secret;
  };

  // Functions for retrieving the current state of the metadata.
  void GetHmac(const std::string& fuzzer_hmac,
               uint64_t label,
               fuzz::span<uint8_t> hmac) const;
  size_t CopyMetadata(uint64_t label,
                      const LeafData& leaf_data,
                      unimported_leaf_data_t* unimported_leaf_data,
                      fuzz::span<uint8_t> buffer) const;
  size_t GetMetadata(uint64_t label,
                     unimported_leaf_data_t* unimported_leaf_data,
                     fuzz::span<uint8_t> buffer) const;
  size_t GetPath(const std::string& fuzzer_hashes,
                 uint64_t label,
                 fuzz::span<uint8_t> path_hashes) const;

  // Store copies of the root hash of the Merkle tree, and label of the leaf
  // associated with a request so that valid get log requests can be generated.
  void LogRootHash(fuzz::span<const uint8_t> root_hash, uint64_t label);
  // Retrieve a root hash from the log at the given index.
  fuzz::span<const uint8_t> GetRootHashFromLog(size_t index) const;
  // Retrieve a leaf label from the log at the given index.
  uint64_t GetLabelFromLog(size_t index) const;

  // Helper functions used by SerializePinweaver to convert
  size_t SerializeResetTree(const fuzz::pinweaver::Request& pinweaver,
                            fuzz::span<uint8_t> buffer) const;
  size_t SerializeInsertLeaf(const fuzz::pinweaver::Request& pinweaver,
                             fuzz::span<uint8_t> buffer) const;
  size_t SerializeRemoveLeaf(const fuzz::pinweaver::Request& pinweaver,
                             fuzz::span<uint8_t> buffer) const;
  size_t SerializeTryAuth(const fuzz::pinweaver::Request& pinweaver,
                          fuzz::span<uint8_t> buffer) const;
  size_t SerializeResetAuth(const fuzz::pinweaver::Request& pinweaver,
                            fuzz::span<uint8_t> buffer) const;
  size_t SerializeGetLog(const fuzz::pinweaver::Request& pinweaver,
                         fuzz::span<uint8_t> buffer) const;
  size_t SerializeLogReplay(const fuzz::pinweaver::Request& pinweaver,
                            fuzz::span<uint8_t> buffer) const;

  // Updates the metadata storage for a particular leaf. |leaf_data| is only
  // required for insert operations so the metadata, low_entropy_secret,
  // and reset_secret for the leaf can be retrieved to generate valid
  // authentication requests.
  void UpdateMetadata(uint64_t label,
                      const pw_response_header_t& header,
                      const unimported_leaf_data_t* unimported_leaf_data,
                      size_t unimported_leaf_data_length,
                      const LeafData* leaf_data);

  // Helper functions for updating the state when responses are received.
  void ApplyResetTree();
  void ApplyInsertLeaf(const fuzz::pinweaver::Request& pinweaver,
                       const pw_response_t& response,
                       const LeafData* leaf_data);
  void ApplyRemoveLeaf(const fuzz::pinweaver::Request& pinweaver,
                       const pw_response_t& response);
  void ApplyTryAuth(const fuzz::pinweaver::Request& pinweaver,
                    const pw_response_t& response);
  void ApplyResetAuth(const fuzz::pinweaver::Request& pinweaver,
                      const pw_response_t& response);

  merkle_tree_t merkle_tree_;

  MemHashTree mem_hash_tree_;
  std::deque<std::pair<std::array<uint8_t, PW_HASH_SIZE>, uint64_t>>
      root_history_;
  std::unordered_map<uint64_t, LeafData> leaf_metadata_;
};

#endif  // __FUZZ_PINWEAVER_MODEL_H
