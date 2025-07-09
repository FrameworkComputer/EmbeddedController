/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>
#include <pb_decode.h>

#include "pw_status/status.h"
#include "pw_transport/service.h"

namespace {

constexpr const uint8_t kTestResponseReadyBit = 2;
constexpr const size_t kTestResponseBufferSize = 128;

static_assert(sizeof(pw_transport_Status::flags_mask) == 2,
              "This test only works for a flags size of 2");

using TestTransportBase =
    pw::transport::Transport<kTestResponseReadyBit, kTestResponseBufferSize>;

class TestTransport : public TestTransportBase {
 public:
  TestTransport() : TestTransportBase() {
    SetNotifyClientCallback([this](bool has_data) { has_data_ = has_data; });
  }

  inline bool HasPendingStatus() const { return has_data_; }

 protected:
 private:
  bool has_data_ = false;
};

bool decode(pw::ConstByteSpan status_result, pw_transport_Status& status) {
  pb_istream_t stream = pb_istream_from_buffer(
      reinterpret_cast<const pb_byte_t*>(status_result.data()),
      status_result.size());
  return pb_decode_delimited(&stream, pw_transport_Status_fields, &status);
}

TEST(TransportLayer, CantReadNextMessageWhenEmpty) {
  TestTransport t;
  EXPECT_EQ(t.ReadNextMessage().status(), pw::Status::NotFound());
  EXPECT_FALSE(t.HasPendingStatus());
}

TEST(TransportLayer, SetStatusBits) {
  TestTransport t;
  ASSERT_TRUE(t.SetStatusBit(0, true).ok());
  ASSERT_TRUE(t.SetStatusBit(9, true).ok());
  ASSERT_EQ(t.SetStatusBit(16, true), pw::Status::OutOfRange());
  EXPECT_TRUE(t.HasPendingStatus());

  auto status_result = t.ReadNextMessage();
  pw_transport_Status status;
  ASSERT_TRUE(status_result.ok());
  ASSERT_TRUE(decode(*status_result, status));
  EXPECT_EQ(status.flags_mask[0], 0x01);
  EXPECT_EQ(status.flags_mask[1], 0x02);
  EXPECT_EQ(status.response_length, 0);
  EXPECT_FALSE(t.HasPendingStatus());
}

TEST(TransportLayer, StageResponse) {
  TestTransport t;

  const int expected_response = 54321;
  ASSERT_TRUE(t.StageResponse<int>(
                   expected_response,
                   [](const int& data, pw::ByteSpan out) -> pw::StatusWithSize {
                     if (out.size() < sizeof(int)) {
                       return pw::StatusWithSize(
                           pw::Status::ResourceExhausted(), 0);
                     }
                     memcpy(out.data(), &data, sizeof(int));
                     return pw::StatusWithSize(sizeof(int));
                   })
                  .ok());

  EXPECT_TRUE(t.HasPendingStatus());
  auto status_result = t.ReadNextMessage();
  pw_transport_Status status;
  ASSERT_TRUE(status_result.ok());
  ASSERT_TRUE(decode(*status_result, status));
  EXPECT_EQ(status.flags_mask[0], (1 << kTestResponseReadyBit) & 0xff);
  EXPECT_EQ(status.flags_mask[1], ((1 << kTestResponseReadyBit) >> 8) & 0xff);
  EXPECT_EQ(status.response_length, sizeof(int));
  EXPECT_FALSE(t.HasPendingStatus());

  auto response_result = t.ReadNextMessage();
  ASSERT_TRUE(response_result.ok());
  ASSERT_EQ(response_result->size(), sizeof(int));
  const auto* value = reinterpret_cast<const int*>(response_result->data());
  ASSERT_EQ(*value, expected_response);

  ASSERT_FALSE(t.ReadNextMessage().ok());
}

TEST(TransportLayer, SetFlagsBeforeResponse) {
  TestTransport t;
  const uint8_t second_flag = kTestResponseReadyBit + 1;
  const uint8_t expected_flags_mask[] = {
      ((1 << kTestResponseReadyBit) & 0xff) | ((1 << second_flag) & 0xff),
      (((1 << kTestResponseReadyBit) >> 8) & 0xff) |
          (((1 << second_flag) >> 8) & 0xff),
  };

  ASSERT_TRUE(t.SetStatusBit(second_flag, true).ok());

  const int expected_response = 54321;
  ASSERT_TRUE(t.StageResponse<int>(
                   expected_response,
                   [](const int& data, pw::ByteSpan out) -> pw::StatusWithSize {
                     if (out.size() < sizeof(int)) {
                       return pw::StatusWithSize(
                           pw::Status::ResourceExhausted(), 0);
                     }
                     memcpy(out.data(), &data, sizeof(int));
                     return pw::StatusWithSize(sizeof(int));
                   })
                  .ok());

  EXPECT_TRUE(t.HasPendingStatus());

  auto status_result = t.ReadNextMessage();
  pw_transport_Status status;
  ASSERT_TRUE(status_result.ok());
  ASSERT_TRUE(decode(*status_result, status));
  EXPECT_EQ(status.flags_mask[0], expected_flags_mask[0]);
  EXPECT_EQ(status.flags_mask[1], expected_flags_mask[1]);
  EXPECT_EQ(status.response_length, sizeof(int));
  EXPECT_FALSE(t.HasPendingStatus());

  auto response_result = t.ReadNextMessage();
  ASSERT_TRUE(response_result.ok());
  ASSERT_EQ(response_result->size(), sizeof(int));
  const auto* value = reinterpret_cast<const int*>(response_result->data());
  ASSERT_EQ(*value, expected_response);
  EXPECT_FALSE(t.HasPendingStatus());

  ASSERT_FALSE(t.ReadNextMessage().ok());
}

TEST(TransportLayer, SetFlagsAfterReadingStatus) {
  TestTransport t;

  const int expected_response = 54321;
  ASSERT_TRUE(t.StageResponse<int>(
                   expected_response,
                   [](const int& data, pw::ByteSpan out) -> pw::StatusWithSize {
                     if (out.size() < sizeof(int)) {
                       return pw::StatusWithSize(
                           pw::Status::ResourceExhausted(), 0);
                     }
                     memcpy(out.data(), &data, sizeof(int));
                     return pw::StatusWithSize(sizeof(int));
                   })
                  .ok());
  EXPECT_TRUE(t.HasPendingStatus());

  auto status_result = t.ReadNextMessage();
  pw_transport_Status status;
  ASSERT_TRUE(status_result.ok());
  ASSERT_TRUE(decode(*status_result, status));
  EXPECT_EQ(status.flags_mask[0], (1 << kTestResponseReadyBit) & 0xff);
  EXPECT_EQ(status.flags_mask[1], ((1 << kTestResponseReadyBit) >> 8) & 0xff);
  EXPECT_EQ(status.response_length, sizeof(int));
  EXPECT_FALSE(t.HasPendingStatus());

  // We already read the status, if we set a bit now, we should still get a
  // response followed by a new state. Since we set the status while there's a
  // result still pending, we should expect to still see no pending status. The
  // pending status will automatically be set once we read the response.
  ASSERT_TRUE(t.SetStatusBit(0, true).ok());
  EXPECT_FALSE(t.HasPendingStatus());

  auto response_result = t.ReadNextMessage();
  ASSERT_TRUE(response_result.ok());
  ASSERT_EQ(response_result->size(), sizeof(int));
  const auto* value = reinterpret_cast<const int*>(response_result->data());
  ASSERT_EQ(*value, expected_response);
  EXPECT_TRUE(t.HasPendingStatus());

  status_result = t.ReadNextMessage();
  ASSERT_TRUE(status_result.ok());
  ASSERT_TRUE(decode(*status_result, status));
  EXPECT_EQ(status.flags_mask[0], 0x01);
  EXPECT_EQ(status.flags_mask[1], 0x00);
  EXPECT_EQ(status.response_length, 0);
  EXPECT_FALSE(t.HasPendingStatus());
}

TEST(TransportLayer, StageResultTwice) {
  TestTransport t;

  auto serialize_fn = [](const int& data,
                         pw::ByteSpan out) -> pw::StatusWithSize {
    if (out.size() < sizeof(int)) {
      return pw::StatusWithSize(pw::Status::Unknown(), 0);
    }
    memcpy(out.data(), &data, sizeof(int));
    return pw::StatusWithSize(sizeof(int));
  };

  const int expected_response = 54321;
  ASSERT_TRUE(t.StageResponse<int>(expected_response, serialize_fn).ok());
  EXPECT_TRUE(t.HasPendingStatus());
  ASSERT_EQ(t.StageResponse<int>(expected_response, serialize_fn),
            pw::Status::ResourceExhausted());
  EXPECT_TRUE(t.HasPendingStatus());
}

TEST(TransportLayer, FailToSerializeResult) {
  TestTransport t;

  auto serialize_fn = [](const int&, pw::ByteSpan) -> pw::StatusWithSize {
    return pw::StatusWithSize(pw::Status::Unknown(), 0);
  };

  const int expected_response = 54321;
  ASSERT_EQ(t.StageResponse<int>(expected_response, serialize_fn),
            pw::Status::Unknown());
  EXPECT_FALSE(t.HasPendingStatus());
}

}  // namespace
