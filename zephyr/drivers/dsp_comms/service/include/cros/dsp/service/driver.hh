/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "cros/dsp/service/cros_transport.hh"
#include "proto/ec_dsp.pb.h"

#define CROS_DSP_RESPONSE_BUFFER_SIZE 128

namespace cros::dsp::util {

template <typename ValueType, typename ClassType>
constexpr size_t OffsetOf(const ValueType ClassType::*member) {
  std::aligned_storage<sizeof(ClassType), alignof(ClassType)> obj_memory;
  ClassType* obj = reinterpret_cast<ClassType*>(&obj_memory);
  return reinterpret_cast<size_t>(&(obj->*member)) -
         reinterpret_cast<size_t>(obj);
}

template <typename ValueType, typename ClassType>
ClassType* ContainerOf(void* ptr, ValueType ClassType::*member) {
  return reinterpret_cast<ClassType*>(reinterpret_cast<char*>(ptr) -
                                      OffsetOf(member));
}

template <typename ValueType, typename ClassType>
const ClassType* ContainerOf(const void* ptr, ValueType ClassType::*member) {
  return reinterpret_cast<const ClassType*>(reinterpret_cast<const char*>(ptr) -
                                            OffsetOf(member));
}

}  // namespace cros::dsp::util

#ifdef __cplusplus
extern "C" {
#endif
int dsp_service_read_requested(struct i2c_target_config* cfg, uint8_t* out);
int dsp_service_read_processed(struct i2c_target_config* cfg, uint8_t* out);
int dsp_service_write_requested(struct i2c_target_config* cfg);
int dsp_service_write_received(struct i2c_target_config* cfg, uint8_t in);
int dsp_service_stop(struct i2c_target_config* cfg);
void dsp_service_buf_write_received(struct i2c_target_config* cfg,
                                    uint8_t* ptr,
                                    uint32_t len);
int dsp_service_buf_read_requested(struct i2c_target_config* cfg,
                                   uint8_t** ptr,
                                   uint32_t* len);

void dsp_service_handle_get_cbi_flags_request(struct k_work* work_item);
void dsp_service_hook_lid_change();
void dsp_service_hook_tablet_mode_change();
#ifdef __cplusplus
}
#endif

namespace cros::dsp::service {

#define CROS_DSP_GPIO_ON 1
#define CROS_DSP_GPIO_OFF 0

class Driver {
 public:
  Driver(uint16_t target_address,
         const struct i2c_target_callbacks* target_callbacks,
         const struct device* bus,
         struct gpio_dt_spec interrupt)
      : target_cfg_{}, bus_(bus), interrupt_(interrupt), transport_() {
    target_cfg_.address = target_address;
    target_cfg_.callbacks = target_callbacks;
  }

  pw::Status Init();

  friend int ::dsp_service_read_requested(struct i2c_target_config* cfg,
                                          uint8_t* out);
  friend int ::dsp_service_read_processed(struct i2c_target_config* cfg,
                                          uint8_t* out);
  friend int ::dsp_service_write_requested(struct i2c_target_config* cfg);
  friend int ::dsp_service_write_received(struct i2c_target_config* cfg,
                                          uint8_t in);
  friend int ::dsp_service_stop(struct i2c_target_config* cfg);
  friend void ::dsp_service_buf_write_received(struct i2c_target_config* cfg,
                                               uint8_t* ptr,
                                               uint32_t len);
  friend int ::dsp_service_buf_read_requested(struct i2c_target_config* cfg,
                                              uint8_t** ptr,
                                              uint32_t* len);
  friend void ::dsp_service_handle_get_cbi_flags_request(
      struct k_work* work_item);
  friend void ::dsp_service_hook_lid_change();
  friend void ::dsp_service_hook_tablet_mode_change();

 private:
  constexpr static const size_t kRequestBufferSize =
      cros_dsp_comms_EcService_size;

  bool HandleDecodedRequest();
  void SetNotebookMode(cros_dsp_comms_NotebookMode mode);
  bool AttemptToDecode();
  struct i2c_target_config target_cfg_;
  const struct device* bus_;
  const struct gpio_dt_spec interrupt_;

  struct k_work get_cbi_flags_work_ = {};

  struct k_sem data_processing_semaphore_ = {};

  cros::dsp::service::CrosTransport transport_;

  struct {
    uint8_t has_status_pending : 1;
    uint8_t has_response_pending : 1;
    uint8_t _reserved : 6;
  } response_state_ = {};

  pw::ConstByteSpan pio_response_buffer_;
  uint8_t pio_response_buffer_position_ = 0;

  uint8_t request_buffer_[kRequestBufferSize] = {};
  uint32_t request_buffer_size_ = 0;
  cros_dsp_comms_EcService pending_service_request_ = {};
};

extern Driver driver;

}  // namespace cros::dsp::service
