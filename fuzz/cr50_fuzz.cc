// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fuzzer for the TPM2 and vendor specific Cr50 commands.

#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <src/libfuzzer/libfuzzer_macro.h>
#include <src/mutator.h>

#define HIDE_EC_STDLIB
#include "chip/host/persistence.h"
#include "fuzz/cr50_fuzz.pb.h"
#include "fuzz/fuzz_config.h"
#include "fuzz/pinweaver_model.h"
#include "fuzz/span.h"
#include "include/nvmem.h"
#include "include/nvmem_vars.h"
#include "include/pinweaver.h"

using protobuf_mutator::libfuzzer::LoadProtoInput;

namespace {
constexpr size_t kBufferAlignment = alignof(pw_request_t) >
                                            alignof(pw_response_t)
                                        ? alignof(pw_request_t)
                                        : alignof(pw_response_t);
}  // namespace

extern "C" uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {NVMEM_TPM_SIZE,
                                                         NVMEM_CR50_SIZE};

extern "C" void rand_bytes(void* data, size_t len) {
  size_t x = 0;

  uint8_t* buffer = reinterpret_cast<uint8_t*>(data);
  for (; x < len; ++x) {
    buffer[x] = rand();
  }
}

extern "C" void get_storage_seed(void* buf, size_t* len) {
  memset(buf, 0x77, *len);
}

extern "C" uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
                                          uint8_t sha256_of_selected_pcr[32]) {
  memset(sha256_of_selected_pcr, 0, 32);
  return 0;
}

// Needed for test targets to build.
extern "C" void run_test(void) {}

void InitializeFuzzerRun() {
  memset(__host_flash, 0xff, sizeof(__host_flash));
  nvmem_init();
  nvmem_enable_commits();
  initvars();
  srand(0);
}

DEFINE_CUSTOM_PROTO_MUTATOR_IMPL(false, fuzz::Cr50FuzzerInput)
DEFINE_CUSTOM_PROTO_CROSSOVER_IMPL(false, fuzz::Cr50FuzzerInput)

extern "C" int test_fuzz_one_input(const uint8_t* data, unsigned int size) {
  fuzz::Cr50FuzzerInput input;
  if (!LoadProtoInput(false, data, size, &input)) {
    return 0;
  }

  InitializeFuzzerRun();

  PinweaverModel pinweaver_model;
  alignas(kBufferAlignment) uint8_t buffer[PW_MAX_MESSAGE_SIZE] = {};
  fuzz::span<uint8_t> buffer_view(buffer, sizeof(buffer));
  for (const fuzz::Cr50SubAction& action : input.sub_actions()) {
    switch (action.sub_action_case()) {
      case fuzz::Cr50SubAction::kRandomBytes:
        fuzz::CopyWithPadding(action.random_bytes().value(), buffer_view, 0);
        pinweaver_model.SendBuffer(buffer_view);
        break;
      case fuzz::Cr50SubAction::kPinweaver:
        pinweaver_model.ApplyRequest(action.pinweaver(), buffer_view);
        break;
      case fuzz::Cr50SubAction::SUB_ACTION_NOT_SET:
        break;
    }
  }
  return 0;
}
