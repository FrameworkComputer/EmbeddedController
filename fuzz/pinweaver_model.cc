// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuzz/pinweaver_model.h"

#include "board/host/dcrypto.h"

namespace {

struct pw_request_t* SerializeCommon(const fuzz::pinweaver::Request& pinweaver,
                                     pw_message_type_t message_type,
                                     fuzz::span<uint8_t> buffer) {
  struct pw_request_t* request =
      reinterpret_cast<struct pw_request_t*>(buffer.begin());
  if (pinweaver.has_version()) {
    request->header.version = pinweaver.version().value();
  } else {
    request->header.version = PW_PROTOCOL_VERSION;
  }
  request->header.type = message_type;
  return request;
}

void CheckBuffer(fuzz::span<uint8_t> buffer) {
  uintptr_t ptr = reinterpret_cast<uintptr_t>(buffer.begin());
  assert(ptr % alignof(pw_request_t) == 0);
  assert(ptr % alignof(pw_response_t) == 0);
}

}  // namespace

//******************************************************************************
// Public member functions.
//******************************************************************************

PinweaverModel::PinweaverModel() {
  Reset();
}

void PinweaverModel::SendBuffer(fuzz::span<uint8_t> buffer) {
  assert(sizeof(pw_request_t) <= buffer.size());
  assert(sizeof(pw_response_t) <= buffer.size());
  CheckBuffer(buffer);
  pw_request_t* request = reinterpret_cast<pw_request_t*>(buffer.begin());
  pw_response_t* response = reinterpret_cast<pw_response_t*>(buffer.begin());
  pw_handle_request(&merkle_tree_, request, response);
}

size_t PinweaverModel::SerializeRequest(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  assert(buffer.size() >= PW_MAX_MESSAGE_SIZE);
  CheckBuffer(buffer);
  switch (pinweaver.request_case()) {
    case fuzz::pinweaver::Request::kResetTree:
      return SerializeResetTree(pinweaver, buffer);
    case fuzz::pinweaver::Request::kInsertLeaf:
      return SerializeInsertLeaf(pinweaver, buffer);
    case fuzz::pinweaver::Request::kRemoveLeaf:
      return SerializeRemoveLeaf(pinweaver, buffer);
    case fuzz::pinweaver::Request::kTryAuth:
      return SerializeTryAuth(pinweaver, buffer);
    case fuzz::pinweaver::Request::kResetAuth:
      return SerializeResetAuth(pinweaver, buffer);
    case fuzz::pinweaver::Request::kGetLog:
      return SerializeGetLog(pinweaver, buffer);
    case fuzz::pinweaver::Request::kLogReplay:
      return SerializeLogReplay(pinweaver, buffer);
    case fuzz::pinweaver::Request::REQUEST_NOT_SET:
      break;
  }
  return 0;
}

uint32_t PinweaverModel::ApplyRequest(const fuzz::pinweaver::Request& pinweaver,
                                      fuzz::span<uint8_t> buffer) {
  SerializeRequest(pinweaver, buffer);
  LeafData leaf_data;

  // Size and alignment of buffer are checked in SerializeRequest().
  pw_request_t* request = reinterpret_cast<pw_request_t*>(buffer.begin());
  pw_response_t* response = reinterpret_cast<pw_response_t*>(buffer.begin());

  if (pinweaver.request_case() == fuzz::pinweaver::Request::kInsertLeaf) {
    pw_request_insert_leaf_t& insert = request->data.insert_leaf;
    std::copy(insert.low_entropy_secret,
              insert.low_entropy_secret + PW_SECRET_SIZE,
              leaf_data.low_entropy_secret.begin());
    std::copy(insert.reset_secret, insert.reset_secret + PW_SECRET_SIZE,
              leaf_data.reset_secret.begin());
  }

  pw_handle_request(&merkle_tree_, request, response);
  if (response->header.result_code != EC_SUCCESS &&
      pinweaver.request_case() != fuzz::pinweaver::Request::kTryAuth) {
    return response->header.result_code;
  }

  switch (pinweaver.request_case()) {
    case fuzz::pinweaver::Request::kResetTree:
      ApplyResetTree();
      break;
    case fuzz::pinweaver::Request::kInsertLeaf:
      ApplyInsertLeaf(pinweaver, *response, &leaf_data);
      break;
    case fuzz::pinweaver::Request::kRemoveLeaf:
      ApplyRemoveLeaf(pinweaver, *response);
      break;
    case fuzz::pinweaver::Request::kTryAuth:
      ApplyTryAuth(pinweaver, *response);
      break;
    case fuzz::pinweaver::Request::kResetAuth:
      ApplyResetAuth(pinweaver, *response);
      break;
    // GetLog and LogReplay have no side-effects so the model doesn't need
    // to be updated.
    case fuzz::pinweaver::Request::kGetLog:
    case fuzz::pinweaver::Request::kLogReplay:
    case fuzz::pinweaver::Request::REQUEST_NOT_SET:
      break;
  }
  return response->header.result_code;
}

void PinweaverModel::Reset() {
  memset(&merkle_tree_, 0, sizeof(merkle_tree_));
  leaf_metadata_.clear();
  mem_hash_tree_.Reset();
  root_history_.clear();
};

//******************************************************************************
// Private static fields.
//******************************************************************************

constexpr uint8_t PinweaverModel::kNullRootHash[PW_HASH_SIZE];

//******************************************************************************
// Private member functions.
//******************************************************************************

void PinweaverModel::GetHmac(const std::string& fuzzer_hmac,
                             uint64_t label,
                             fuzz::span<uint8_t> hmac) const {
  assert(hmac.size() == PW_HASH_SIZE);
  if (!fuzzer_hmac.empty()) {
    fuzz::CopyWithPadding(fuzzer_hmac, hmac, 0);
    return;
  }
  mem_hash_tree_.GetLeaf(label, hmac);
}

size_t PinweaverModel::CopyMetadata(
    uint64_t label,
    const LeafData& leaf_data,
    unimported_leaf_data_t* unimported_leaf_data,
    fuzz::span<uint8_t> buffer) const {
  const std::vector<uint8_t>& data = leaf_data.wrapped_data;
  memcpy(unimported_leaf_data, data.data(), data.size());

  fuzz::span<uint8_t> path_hashes(
      reinterpret_cast<uint8_t*>(unimported_leaf_data) + data.size(),
      buffer.end());
  return data.size() + mem_hash_tree_.GetPath(label, path_hashes);
}

size_t PinweaverModel::GetMetadata(uint64_t label,
                                   unimported_leaf_data_t* unimported_leaf_data,
                                   fuzz::span<uint8_t> buffer) const {
  auto itr = leaf_metadata_.find(label);
  if (itr == leaf_metadata_.end()) {
    assert(buffer.size() >= sizeof(wrapped_leaf_data_t));
    std::fill(buffer.begin(), buffer.begin() + sizeof(wrapped_leaf_data_t), 0);
    return sizeof(wrapped_leaf_data_t);
  }
  return CopyMetadata(label, itr->second, unimported_leaf_data, buffer);
}

size_t PinweaverModel::GetPath(const std::string& fuzzer_hashes,
                               uint64_t label,
                               fuzz::span<uint8_t> path_hashes) const {
  if (!fuzzer_hashes.empty()) {
    return fuzz::CopyWithPadding(fuzzer_hashes, path_hashes, 0);
  }
  return mem_hash_tree_.GetPath(label, path_hashes);
}

void PinweaverModel::LogRootHash(fuzz::span<const uint8_t> root_hash,
                                 uint64_t label) {
  assert(root_hash.size() == PW_HASH_SIZE);
  std::pair<std::vector<uint8_t>, uint64_t> entry{
      {root_hash.begin(), root_hash.end()}, label};
  if (root_history_.size() == PW_LOG_ENTRY_COUNT) {
    root_history_.pop_front();
  }
  root_history_.emplace_back(std::array<uint8_t, PW_HASH_SIZE>{}, label);
  std::copy(root_hash.begin(), root_hash.end(),
            root_history_.back().first.begin());
}

fuzz::span<const uint8_t> PinweaverModel::GetRootHashFromLog(
    size_t index) const {
  if (index >= root_history_.size()) {
    return fuzz::span<const uint8_t>(kNullRootHash, PW_HASH_SIZE);
  }
  return root_history_.rbegin()[index].first;
}

uint64_t PinweaverModel::GetLabelFromLog(size_t index) const {
  if (index >= root_history_.size()) {
    return 0;
  }
  return root_history_.rbegin()[index].second;
}

size_t PinweaverModel::SerializeResetTree(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::ResetTree& fuzzer_data = pinweaver.reset_tree();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_RESET_TREE}, buffer);
  pw_request_reset_tree_t* req_data = &request->data.reset_tree;

  request->header.data_length = sizeof(*req_data);
  req_data->bits_per_level.v = fuzzer_data.bits_per_level();
  req_data->height.v = fuzzer_data.height();

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeInsertLeaf(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::InsertLeaf& fuzzer_data = pinweaver.insert_leaf();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_INSERT_LEAF}, buffer);
  pw_request_insert_leaf_t* req_data = &request->data.insert_leaf;

  req_data->label.v = fuzzer_data.label();
  fuzz::CopyWithPadding(
      fuzzer_data.delay_schedule(),
      fuzz::span<uint8_t>(reinterpret_cast<uint8_t*>(req_data->delay_schedule),
                          sizeof(req_data->delay_schedule)),
      0);
  fuzz::CopyWithPadding(
      fuzzer_data.low_entropy_secret(),
      fuzz::span<uint8_t>(req_data->low_entropy_secret, PW_SECRET_SIZE), 0);
  fuzz::CopyWithPadding(
      fuzzer_data.high_entropy_secret(),
      fuzz::span<uint8_t>(req_data->high_entropy_secret, PW_SECRET_SIZE), 0);
  fuzz::CopyWithPadding(
      fuzzer_data.reset_secret(),
      fuzz::span<uint8_t>(req_data->reset_secret, PW_SECRET_SIZE), 0);

  fuzz::span<uint8_t> path_hashes(
      reinterpret_cast<uint8_t*>(req_data->path_hashes), buffer.end());
  size_t path_hash_size =
      GetPath(fuzzer_data.path_hashes(), fuzzer_data.label(), path_hashes);
  request->header.data_length = sizeof(*req_data) + path_hash_size;

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeRemoveLeaf(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::RemoveLeaf& fuzzer_data = pinweaver.remove_leaf();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_REMOVE_LEAF}, buffer);
  pw_request_remove_leaf_t* req_data = &request->data.remove_leaf;

  req_data->leaf_location.v = fuzzer_data.label();
  GetHmac(fuzzer_data.leaf_hmac(), fuzzer_data.label(),
          fuzz::span<uint8_t>(req_data->leaf_hmac, PW_HASH_SIZE));

  fuzz::span<uint8_t> path_hashes(
      reinterpret_cast<uint8_t*>(req_data->path_hashes), buffer.end());
  size_t path_hash_size =
      GetPath(fuzzer_data.path_hashes(), fuzzer_data.label(), path_hashes);
  request->header.data_length = sizeof(*req_data) + path_hash_size;

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeTryAuth(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::TryAuth& fuzzer_data = pinweaver.try_auth();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_TRY_AUTH}, buffer);
  pw_request_try_auth_t* req_data = &request->data.try_auth;

  request->header.data_length =
      sizeof(*req_data) - sizeof(req_data->unimported_leaf_data);

  auto itr = leaf_metadata_.find(fuzzer_data.label());
  if (fuzzer_data.low_entropy_secret().empty() && itr != leaf_metadata_.end()) {
    const auto& low_entropy_secret = itr->second.low_entropy_secret;
    std::copy(low_entropy_secret.begin(), low_entropy_secret.end(),
              req_data->low_entropy_secret);
  } else {
    fuzz::CopyWithPadding(
        fuzzer_data.low_entropy_secret(),
        fuzz::span<uint8_t>(req_data->low_entropy_secret, PW_SECRET_SIZE), 0);
  }

  if (fuzzer_data.unimported_leaf_data().empty() &&
      itr != leaf_metadata_.end()) {
    request->header.data_length +=
        CopyMetadata(fuzzer_data.label(), itr->second,
                     &req_data->unimported_leaf_data, buffer);
  } else {
    request->header.data_length += fuzz::CopyWithPadding(
        fuzzer_data.unimported_leaf_data(),
        fuzz::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&req_data->unimported_leaf_data),
            sizeof(wrapped_leaf_data_t)),
        0);
  }

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeResetAuth(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::ResetAuth& fuzzer_data = pinweaver.reset_auth();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_RESET_AUTH}, buffer);
  pw_request_reset_auth_t* req_data = &request->data.reset_auth;

  request->header.data_length =
      sizeof(*req_data) - sizeof(req_data->unimported_leaf_data);

  auto itr = leaf_metadata_.find(fuzzer_data.label());
  if (fuzzer_data.reset_secret().empty() && itr != leaf_metadata_.end()) {
    const auto& reset_secret = itr->second.reset_secret;
    std::copy(reset_secret.begin(), reset_secret.end(), req_data->reset_secret);
  } else {
    fuzz::CopyWithPadding(
        fuzzer_data.reset_secret(),
        fuzz::span<uint8_t>(req_data->reset_secret, PW_SECRET_SIZE), 0);
  }

  if (fuzzer_data.unimported_leaf_data().empty() &&
      itr != leaf_metadata_.end()) {
    request->header.data_length +=
        CopyMetadata(fuzzer_data.label(), itr->second,
                     &req_data->unimported_leaf_data, buffer);
  } else {
    request->header.data_length += fuzz::CopyWithPadding(
        fuzzer_data.unimported_leaf_data(),
        fuzz::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&req_data->unimported_leaf_data),
            sizeof(wrapped_leaf_data_t)),
        0);
  }

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeGetLog(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::GetLog& fuzzer_data = pinweaver.get_log();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_GET_LOG}, buffer);
  pw_request_get_log_t* req_data = &request->data.get_log;

  memcpy(req_data->root,
         GetRootHashFromLog(fuzzer_data.index_of_root()).begin(), PW_HASH_SIZE);
  request->header.data_length = sizeof(*req_data);

  return request->header.data_length + sizeof(request->header);
}

size_t PinweaverModel::SerializeLogReplay(
    const fuzz::pinweaver::Request& pinweaver,
    fuzz::span<uint8_t> buffer) const {
  const fuzz::pinweaver::LogReplay& fuzzer_data = pinweaver.log_replay();
  pw_request_t* request = SerializeCommon(pinweaver, {PW_LOG_REPLAY}, buffer);
  pw_request_log_replay_t* req_data = &request->data.log_replay;

  memcpy(req_data->log_root,
         GetRootHashFromLog(fuzzer_data.index_of_root()).begin(), PW_HASH_SIZE);
  request->header.data_length =
      sizeof(*req_data) - sizeof(req_data->unimported_leaf_data);

  if (fuzzer_data.unimported_leaf_data().empty()) {
    request->header.data_length +=
        GetMetadata(GetLabelFromLog(fuzzer_data.index_of_root()),
                    &req_data->unimported_leaf_data, buffer);
  } else {
    request->header.data_length += fuzz::CopyWithPadding(
        fuzzer_data.unimported_leaf_data(),
        fuzz::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&req_data->unimported_leaf_data),
            sizeof(wrapped_leaf_data_t)),
        0);
  }

  return request->header.data_length + sizeof(request->header);
}

void PinweaverModel::UpdateMetadata(
    uint64_t label,
    const pw_response_header_t& header,
    const unimported_leaf_data_t* unimported_leaf_data,
    size_t unimported_leaf_data_length,
    const LeafData* leaf_data) {
  LogRootHash(fuzz::span<const uint8_t>(header.root, PW_HASH_SIZE), label);
  if (unimported_leaf_data) {
    const uint8_t* data =
        reinterpret_cast<const uint8_t*>(unimported_leaf_data);
    LeafData& stored_leaf_data = leaf_metadata_[label];
    if (leaf_data) {
      stored_leaf_data = *leaf_data;
    }
    stored_leaf_data.wrapped_data.assign(data,
                                         data + unimported_leaf_data_length);
    mem_hash_tree_.UpdatePath(
        label,
        fuzz::span<const uint8_t>(unimported_leaf_data->hmac, PW_HASH_SIZE));
  } else {
    leaf_metadata_.erase(label);
    mem_hash_tree_.UpdatePath(label, fuzz::span<const uint8_t>() /*path_hash*/);
  }
}

void PinweaverModel::ApplyResetTree() {
  leaf_metadata_.clear();
  mem_hash_tree_.Reset(merkle_tree_.bits_per_level.v, merkle_tree_.height.v);
}

void PinweaverModel::ApplyInsertLeaf(const fuzz::pinweaver::Request& pinweaver,
                                     const pw_response_t& response,
                                     const LeafData* leaf_data) {
  const pw_response_insert_leaf_t* resp = &response.data.insert_leaf;
  size_t unimported_leaf_data_length = response.header.data_length -
                                       sizeof(*resp) +
                                       sizeof(resp->unimported_leaf_data);
  UpdateMetadata(pinweaver.insert_leaf().label(), response.header,
                 &resp->unimported_leaf_data, unimported_leaf_data_length,
                 leaf_data);
}

void PinweaverModel::ApplyRemoveLeaf(const fuzz::pinweaver::Request& pinweaver,
                                     const pw_response_t& response) {
  UpdateMetadata(pinweaver.remove_leaf().label(), response.header,
                 nullptr /*unimported_leaf_data*/,
                 0 /*unimported_leaf_data_length*/, nullptr /*leaf_data*/);
}

void PinweaverModel::ApplyTryAuth(const fuzz::pinweaver::Request& pinweaver,
                                  const pw_response_t& response) {
  const pw_response_try_auth_t* resp = &response.data.try_auth;

  if (response.header.result_code != EC_SUCCESS &&
      response.header.result_code != PW_ERR_LOWENT_AUTH_FAILED) {
    return;
  }
  size_t unimported_leaf_data_length = response.header.data_length -
                                       sizeof(*resp) +
                                       sizeof(resp->unimported_leaf_data);
  UpdateMetadata(pinweaver.try_auth().label(), response.header,
                 &resp->unimported_leaf_data, unimported_leaf_data_length,
                 nullptr /*leaf_data*/);
}

void PinweaverModel::ApplyResetAuth(const fuzz::pinweaver::Request& pinweaver,
                                    const pw_response_t& response) {
  const pw_response_reset_auth_t* resp = &response.data.reset_auth;
  size_t unimported_leaf_data_length = response.header.data_length -
                                       sizeof(*resp) +
                                       sizeof(resp->unimported_leaf_data);
  UpdateMetadata(pinweaver.reset_auth().label(), response.header,
                 &resp->unimported_leaf_data, unimported_leaf_data_length,
                 nullptr /*leaf_data*/);
}
