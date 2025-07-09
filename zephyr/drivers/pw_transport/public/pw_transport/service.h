/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <pb_encode.h>

#include "pw_bytes/span.h"
#include "pw_fsm/fsm.h"
#include "pw_function/function.h"
#include "pw_result/result.h"
#include "pw_status/status.h"
#include "pw_status/status_with_size.h"
#include "pw_transport/proto/transport.pb.h"

namespace pw::transport {

namespace impl {
enum ServiceState {
  IDLE,
  HAS_STATUS,
  HAS_STATUS_AND_RESPONSE,
  HAS_RESPONSE,
};

inline constexpr pw::fsm::FsmConfig kTransportFsmConfig(
    ServiceState::IDLE,
    pw::fsm::Transition<ServiceState>(ServiceState::IDLE,
                                      ServiceState::HAS_STATUS),
    pw::fsm::Transition<ServiceState>(ServiceState::IDLE,
                                      ServiceState::HAS_STATUS_AND_RESPONSE),
    pw::fsm::Transition<ServiceState>(ServiceState::HAS_STATUS,
                                      ServiceState::IDLE),
    pw::fsm::Transition<ServiceState>(ServiceState::HAS_STATUS,
                                      ServiceState::HAS_STATUS_AND_RESPONSE),
    pw::fsm::Transition<ServiceState>(ServiceState::HAS_STATUS_AND_RESPONSE,
                                      ServiceState::HAS_RESPONSE),
    pw::fsm::Transition<ServiceState>(ServiceState::HAS_RESPONSE,
                                      ServiceState::HAS_STATUS),
    pw::fsm::Transition<ServiceState>(ServiceState::HAS_RESPONSE,
                                      ServiceState::IDLE));
static_assert(kTransportFsmConfig.IsValid());
}  // namespace impl

/**
 * Provide a core Transport functionality for RPCs over hardware buses.
 *
 * This class is designed to work on I2C, SPI, and ESPI.
 *
 * @tparam kStatusFlagHasResponse The bit position of the flags_mask field in
 * the status which will indicate that data is available to the client.
 * @tparam kResponseBufferSize The size of the response buffer. Must be big
 * enough to contain any message serialized in StageResponse.
 */
template <uint8_t kStatusFlagHasResponse, size_t kResponseBufferSize>
class Transport {
 private:
  using StatusType = pw_transport_Status;
  static constexpr auto kStatusBufferSize = pw_transport_Status_size + 4;
  static constexpr auto kFlagsByteCount = sizeof(StatusType::flags_mask);

 public:
  /**
   * A template function type used to serialize data objects.
   */
  template <typename T>
  using SerializeFn =
      pw::Function<pw::StatusWithSize(const T& data, pw::ByteSpan out)>;

  /**
   * Construct a new Transport object.
   *
   */
  constexpr Transport()
      : status_{}, response_buffer_(), status_buffer_(), state_machine_() {}

  virtual ~Transport() = default;

  /**
   * Set a listener for then data becomes available.
   *
   * When new status bits are set, notify the callback that there's data. When
   * the status bits are cleared by a client read, notify the client. This is
   * generally used to control a GPIO being level active when there's data and
   * inactive when there's none.
   *
   * Note that this will only be called on the first status bit being set.
   * Setting 2 bits will not result in 2 callbacks.
   *
   * @param notify_client_fn A function to call when the status bits are set or
   * cleared.
   */
  void SetNotifyClientCallback(
      pw::Function<void(bool has_data)>&& notify_client_fn) {
    state_machine_.SetNotifyClientCallback(std::move(notify_client_fn));
  }

  /**
   * Stage a response if possible.
   *
   * Responses can only be staged if there isn't one currently staged.
   *
   * @tparam T The response type.
   * @param response A const reference to the response which needs to be
   * serialized.
   * @param serialize A function which will serialize the response.
   * @return pw::OkStatus on success, anything else on failure.
   */
  template <typename T>
  pw::Status StageResponse(const T& response, SerializeFn<T>&& serialize) {
    auto transaction = state_machine_.BeginTransaction(
        impl::ServiceState::HAS_STATUS_AND_RESPONSE);
    if (!transaction.ok()) {
      return pw::Status::ResourceExhausted();
    }

    transaction->Do(
        [this](const T& response, SerializeFn<T> serialize) {
          // Serialize the result
          auto serialize_result =
              serialize(response, response_buffer_.FullSpan());
          if (!serialize_result.ok()) {
            return serialize_result.status();
          }

          response_buffer_.SetSize(serialize_result.size());

          // Update the size field
          return UpdateResponseSize(serialize_result.size());
        },
        response,
        std::move(serialize));

    // Update the status flag to signal there's data
    transaction->Do(
        [this]() { return DoSetStatusBit(kStatusFlagHasResponse, true); });

    return transaction->Commit();
  }

  /**
   * Set a status bit.
   *
   * Set a status bit and call the appropriate client callbacks if the status
   * fields were previously 0.
   *
   * @param bit_num The bit index to set.
   * @param on True to set the bit to 1, false to set the bit to 0.
   * @return pw::OkStatus() on success.
   * @return pw::Status::OutOfRange() if the bit_num is out of range.
   * @return other error if anything went wrong.
   */
  pw::Status SetStatusBit(uint8_t bit_num, bool on) {
    if (auto status = DoSetStatusBit(bit_num, on); !status.ok()) {
      return status;
    }

    if (state_machine_.current_state() == impl::ServiceState::IDLE) {
      auto transaction =
          state_machine_.BeginTransaction(impl::ServiceState::HAS_STATUS);
      if (!transaction.ok()) {
        return transaction.status();
      }
      return transaction->Commit();
    } else {
      state_machine_.NotifyClient();
    }
    return pw::OkStatus();
  }

  /**
   * Get the next message to respond with (if any).
   *
   * Once this function returns, the memory is recycled. It is up to the caller
   * to make a copy if it needs to outlive the current thread context.
   *
   * @return An OK result wrapping a ConstByteSpan which should be serialized to
   * the client.
   */
  pw::Result<pw::ConstByteSpan> ReadNextMessage() {
    if (state_machine_.StateAnyOf(
            {impl::ServiceState::HAS_STATUS,
             impl::ServiceState::HAS_STATUS_AND_RESPONSE})) {
      return ReadStatus();
    }

    if (state_machine_.current_state() == impl::ServiceState::HAS_RESPONSE) {
      return ReadResponse();
    }

    return pw::Status::NotFound();
  }

 protected:
  /**
   * Clear the currently staged status.
   *
   * This function is called after the status is serialized. Normally the status
   * is reset to the empty struct '{}', but if some bits are sticky, a child
   * class may override this function and set a custom clearing operation.
   *
   * @param status The status object to be cleared.
   */
  virtual void ClearStatus(StatusType& status) { status = {}; }

 private:
  /**
   * Serialize the currently staged status.
   *
   * This is a helper function called from ReadStatus.
   *
   * @param status A reference to the current status to serialize
   * @param out An output buffer to write the status to
   * @return The serialization pw::Status along with the size
   */
  pw::StatusWithSize SerializeStatus(const StatusType& status,
                                     pw::ByteSpan out) {
    pb_ostream_t response_ostream = pb_ostream_from_buffer(
        reinterpret_cast<pb_type_t*>(out.data()), out.size());
    bool response_encode_success = pb_encode_delimited(
        &response_ostream, pw_transport_Status_fields, &status);
    if (!response_encode_success) {
      return pw::StatusWithSize(pw::Status::ResourceExhausted(), 0);
    }

    return pw::StatusWithSize(response_ostream.bytes_written);
  }

  /**
   * Read the currently staged status.
   *
   * Assuming there is a status that can be read, serialize the status and
   * return the serialized bytes. Once serialized, the status is cleared via the
   * ClearStatus function.
   *
   * @return pw::Result containing the status of the read operation along with
   * the bytes if pw::OkStatus()
   */
  pw::Result<pw::ConstByteSpan> ReadStatus() {
    impl::ServiceState next_state =
        (state_machine_.current_state() == impl::ServiceState::HAS_STATUS)
            ? impl::ServiceState::IDLE
            : impl::ServiceState::HAS_RESPONSE;
    auto transaction = state_machine_.BeginTransaction(next_state);
    if (!transaction.ok()) {
      return transaction.status();
    }

    transaction->Do([this]() {
      auto serialize_result =
          SerializeStatus(status_, status_buffer_.FullSpan());
      if (!serialize_result.ok()) {
        return serialize_result.status();
      }
      ClearStatus(status_);
      is_status_dirty_ = false;
      status_buffer_.SetSize(serialize_result.size());
      return pw::OkStatus();
    });

    if (auto status = transaction->Commit(); !status.ok()) {
      return status;
    }

    return status_buffer_.DataSpan();
  }

  /**
   * Read the currently staged response
   *
   * Assuming there is a response that can be read, return the response and
   * update the internal state. When a response is read we would transition to
   * either the IDLE state (if the status isn't marked as dirty) or HAS_STATUS
   * if the status has been modified since the last ReadStatus().
   *
   * @return pw::Result with the response bytes assuming one was staged.
   */
  pw::Result<pw::ConstByteSpan> ReadResponse() {
    // At this point we may have gotten a new status bit since we sent the
    // last one. Check to see if the status is empty.
    impl::ServiceState new_state = is_status_dirty_
                                       ? impl::ServiceState::HAS_STATUS
                                       : impl::ServiceState::IDLE;

    auto transaction = state_machine_.BeginTransaction(new_state);
    if (!transaction.ok()) {
      return transaction.status();
    }
    if (auto status = transaction->Commit(); !status.ok()) {
      return status;
    }
    return response_buffer_.DataSpan();
  }

  /**
   * Update a status bit and flag the status dirty.
   *
   * @param bit_num The bit index to update
   * @param on True if the bit should be set to 1, false for 0
   * @return pw::Status::OutOfRange() if the bit_num was out of bounds
   * @return pw::OkStatus() if the bit was updated
   */
  pw::Status DoSetStatusBit(uint8_t bit_num, bool on) {
    if (bit_num / 8 >= kFlagsByteCount) {
      return pw::Status::OutOfRange();
    }

    uint8_t byte_index = bit_num / 8;
    uint8_t bit_mask = 1 << (bit_num % 8);

    if (on) {
      status_.flags_mask[byte_index] |= bit_mask;
    } else {
      status_.flags_mask[byte_index] &= ~bit_mask;
    }
    is_status_dirty_ = true;
    return pw::OkStatus();
  }

  /**
   * Helper function for updating the response size.
   *
   * Verifies that the new_size is within the bounds of the
   * StatusType::response_length field.
   *
   * @param new_size The new size of the serialized response
   * @return pw::Status::InvalidArgument() if the new_size is too big
   * @return pw::OkStatus() if the response length was updated
   */
  pw::Status UpdateResponseSize(size_t new_size) {
    if (new_size >
        std::numeric_limits<decltype(StatusType::response_length)>::max()) {
      return pw::Status::InvalidArgument();
    }
    status_.response_length = new_size;
    return pw::OkStatus();
  }

  /**
   * A custom buffer class. We can't use a pw::Vector here because the vector
   * doesn't allow us to update the size independently from pushing the data.
   * Since this buffer will be used by pb_encode(), we need to maintain the size
   * and data separately.
   */
  template <size_t kCapacity>
  class Buffer {
   public:
    constexpr Buffer() = default;
    void Clear() { memset(buffer_, 0, kCapacity); }
    void SetSize(size_t size) {
      PW_ASSERT(size <= kCapacity);
      size_ = size;
    }
    pw::ByteSpan FullSpan() { return pw::ByteSpan(buffer_, capacity_); }
    pw::ByteSpan DataSpan() { return pw::ByteSpan(buffer_, size_); }

   private:
    std::byte buffer_[kCapacity] = {};
    const size_t capacity_ = kCapacity;
    size_t size_ = 0;
  };

  /**
   * The state machine used by the service transport.
   *
   * This state machine maintains the rules set by kTransportFsmConfig. It is
   * also responsible for notifying the client when the transport has or no
   * longer has a new status that needs to be read.
   */
  class TransportStateMachine
      : public pw::fsm::StateMachine<impl::ServiceState> {
   public:
    constexpr TransportStateMachine()
        : pw::fsm::StateMachine<impl::ServiceState>(impl::kTransportFsmConfig),
          notify_client_fn_(nullptr) {}

    void SetNotifyClientCallback(
        pw::Function<void(bool has_data)>&& notify_client_fn) {
      notify_client_fn_ = std::move(notify_client_fn);
    }

    void NotifyClient() { OnEnter(current_state()); }

   protected:
    void OnEnter(const impl::ServiceState& state) override {
      switch (state) {
        case impl::ServiceState::HAS_STATUS:
        case impl::ServiceState::HAS_STATUS_AND_RESPONSE:
          notify_client_fn_(true);
          return;
        case impl::ServiceState::IDLE:
        case impl::ServiceState::HAS_RESPONSE:
          notify_client_fn_(false);
          return;
      }
    }

   private:
    pw::Function<void(bool has_data)> notify_client_fn_;
  };

  StatusType status_;
  Buffer<kResponseBufferSize> response_buffer_;
  Buffer<kStatusBufferSize> status_buffer_;
  TransportStateMachine state_machine_;
  bool is_status_dirty_ = false;
};

}  // namespace pw::transport
