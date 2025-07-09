/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <pb_encode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "proto/ec_dsp.pb.h"
#include "pw_transport/service.h"

namespace cros::dsp::service {

// TODO Automatically calculate array_size from args
template <size_t array_size, typename... Args>
constexpr auto MakeBitMasks(Args... args) {
  // Create an array filled with zeros
  std::array<std::byte, array_size> result{};

  // Set the bits
  ((result[(args / 8)] |= std::byte{1} << (args % 8)), ...);

  return result;
}

using CrosTransportParent = pw::transport::Transport<
    cros_dsp_comms_StatusFlag_STATUS_FLAG_RESPONSE_READY,
    CONFIG_PLATFORM_EC_DSP_RESPONSE_BUFFER_SIZE>;
template <int b>
consteval size_t AsBit() {
  return static_cast<size_t>(b);
}
inline constexpr auto kBitsToKeep =
    MakeBitMasks<sizeof(pw_transport_Status::flags_mask)>(
        static_cast<size_t>(cros_dsp_comms_StatusFlag_STATUS_FLAG_LID_OPEN),
        static_cast<size_t>(cros_dsp_comms_StatusFlag_STATUS_FLAG_TABLET_MODE));

class CrosTransport : public CrosTransportParent {
 private:
 public:
  constexpr CrosTransport() : CrosTransportParent() {}
  ~CrosTransport() = default;

  pw::Status StageResponse(const cros_dsp_comms_GetCbiFlagsResponse& response) {
    return this->template StageResponse<cros_dsp_comms_GetCbiFlagsResponse>(
        response,
        [](const auto& response, pw::ByteSpan out) -> pw::StatusWithSize {
          pb_ostream_t response_ostream = pb_ostream_from_buffer(
              reinterpret_cast<pb_type_t*>(out.data()), out.size());
          bool response_encode_success =
              pb_encode(&response_ostream,
                        cros_dsp_comms_GetCbiFlagsResponse_fields,
                        &response);
          if (!response_encode_success) {
            return pw::StatusWithSize(pw::Status::ResourceExhausted(), 0);
          }

          return pw::StatusWithSize(response_ostream.bytes_written);
        });
  }

  using CrosTransportParent::SetNotifyClientCallback;

 protected:
  using CrosTransportParent::StageResponse;

  void ClearStatus(pw_transport_Status& status) {
    status.response_length = 0;
    for (size_t i = 0; i < sizeof(pw_transport_Status::flags_mask); ++i) {
      uint8_t mask = (i >= kBitsToKeep.size())
                         ? 0
                         : std::to_integer<uint8_t>(kBitsToKeep[i]);
      status.flags_mask[i] &= mask;
    }
  }
};

}  // namespace cros::dsp::service
