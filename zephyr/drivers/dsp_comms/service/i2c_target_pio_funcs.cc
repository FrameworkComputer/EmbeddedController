/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <cerrno>

#include "cros/dsp/service/driver.hh"

LOG_MODULE_DECLARE(dsp_service, CONFIG_DSP_COMMS_LOG_LEVEL);

extern "C" int dsp_service_write_requested(struct i2c_target_config*) {
  // We have a new request, clear the buffer.

  LOG_DBG("Taking semaphore...");
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't start a new write at this time");
    return -EIO;
  }

  cros::dsp::service::driver.request_buffer_size_ = 0;
  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  return 0;
}

extern "C" int dsp_service_write_received(struct i2c_target_config*,
                                          uint8_t in) {
  LOG_DBG("Taking semaphore...");
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't process more bytes at this time");
    return -EIO;
  }

  // Check that we have room
  if (cros::dsp::service::driver.request_buffer_size_ >=
      cros_dsp_comms_EcService_size) {
    LOG_ERR("request_buffer_size (%uB) overflow, capacity is %uB",
            cros::dsp::service::driver.request_buffer_size_,
            cros_dsp_comms_EcService_size);
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return -EIO;
  }

  // Write the byte
  cros::dsp::service::driver
      .request_buffer_[cros::dsp::service::driver.request_buffer_size_++] = in;
  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  return 0;
}

extern "C" int dsp_service_read_requested(struct i2c_target_config*,
                                          uint8_t* out) {
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't start a read");
    return -EIO;
  }

  LOG_DBG("Reading next message...");
  auto response = cros::dsp::service::driver.transport_.ReadNextMessage();
  if (!response.ok() || response->size() == 0) {
    LOG_ERR("No pending response");
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return -EIO;
  }

  cros::dsp::service::driver.pio_response_buffer_ = *response;
  *out =
      static_cast<uint8_t>(cros::dsp::service::driver.pio_response_buffer_[0]);
  cros::dsp::service::driver.pio_response_buffer_position_ = 1;

  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  return 0;
}

extern "C" int dsp_service_read_processed(struct i2c_target_config*,
                                          uint8_t* out) {
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't start a read");
    return -EIO;
  }

  if (cros::dsp::service::driver.pio_response_buffer_position_ >=
      cros::dsp::service::driver.pio_response_buffer_.size()) {
    *out = 0;
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return 0;
  }

  *out = static_cast<uint8_t>(
      cros::dsp::service::driver.pio_response_buffer_
          [cros::dsp::service::driver.pio_response_buffer_position_++]);
  k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  return 0;
}

extern "C" int dsp_service_stop(struct i2c_target_config*) {
  LOG_DBG("Done");
  if (k_sem_take(&cros::dsp::service::driver.data_processing_semaphore_,
                 K_NO_WAIT) != 0) {
    LOG_ERR("Can't process stop");
    return -EIO;
  }

  if (cros::dsp::service::driver.request_buffer_size_ == 0) {
    // There'd no new data to decode, just bail.
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return 0;
  }

  // Try to decode
  bool is_decoded = cros::dsp::service::driver.AttemptToDecode();

  if (!is_decoded) {
    // Failed to decode, wait for more bytes
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
    return 0;
  }

  cros::dsp::service::driver.request_buffer_size_ = 0;

  LOG_DBG("deasserting GPIO...");
  int rc = gpio_pin_set_dt(&cros::dsp::service::driver.interrupt_,
                           CROS_DSP_GPIO_OFF);
  LOG_DBG("deasserting GPIO (%d)", rc);
  if (!cros::dsp::service::driver.HandleDecodedRequest()) {
    // We did not scheduled a work item, release the semaphore
    k_sem_give(&cros::dsp::service::driver.data_processing_semaphore_);
  }
  return 0;
}
