/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "cros/dsp/service/driver.hh"

LOG_MODULE_DECLARE(dsp_service, CONFIG_DSP_COMMS_LOG_LEVEL);

extern "C" void dsp_service_buf_write_received(struct i2c_target_config*,
                                               uint8_t* ptr,
                                               uint32_t len) {
  LOG_DBG("%s: Taking semaphore...", __FUNCTION__);
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't process a request at this time");
    return;
  }

  // Check capacity
  if (len > cros_dsp_comms_EcService_size) {
    LOG_ERR("Overflow, trying to write (%uB), capacity is %uB",
            len,
            cros_dsp_comms_EcService_size);
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return;
  }

  LOG_DBG("Writing %u bytes", len);

  // Set the size and copy the data
  cros::dsp::service::driver.request_buffer_size_ = len;
  memcpy(cros::dsp::service::driver.request_buffer_, ptr, len);
  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
}

extern "C" int dsp_service_buf_read_requested(struct i2c_target_config*,
                                              uint8_t** ptr,
                                              uint32_t* len) {
  LOG_DBG("%s: Taking semaphore...", __FUNCTION__);
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't process a request at this time");
    return -EBUSY;
  }

  auto response = cros::dsp::service::driver.transport_.ReadNextMessage();
  if (!response.ok()) {
    LOG_ERR("No pending response");
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return -ENODATA;
  }

  // Yes, really remove const, Zephyr upstream should make the ptr a const.
  *ptr = reinterpret_cast<uint8_t*>(const_cast<std::byte*>(response->data()));
  *len = response->size();
  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  return 0;
}
