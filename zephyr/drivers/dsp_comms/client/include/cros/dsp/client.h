/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EC_ZEPHYR_DRIVERS_DSP_COMMS_INCLUDE_CROS_DSP_CLIENT_H_
#define EC_ZEPHYR_DRIVERS_DSP_COMMS_INCLUDE_CROS_DSP_CLIENT_H_

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include "ec_commands.h"
#include "proto/ec_dsp.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Use the DSP API to read a set of cbi flags
 *
 * @param dev The DSP client device, usually default_client_device but may be
 * set to something else in testing.
 * @param flag The CBI flag to read.
 * @param mem The memory to write the response to
 * @return 0 on success
 * @return < 0 on error
 */
int dsp_client_get_cbi_flags(const struct device* dev,
                             cros_dsp_comms_CbiFlag flag,
                             cros_dsp_comms_GetCbiFlagsResponse* mem);

/**
 * A shim entry point to the DSP client.
 *
 * Calls dsp_client_get_cbi_flags() by converting the tag to the correct flag
 * and using default_client_device as the DSP client.
 *
 * @param tag The CBI data tag to read
 * @param buffer The buffer to store the data into
 * @param buffer_size Set initially to the inbound buffer size, and upon
 * successful return, set to the number of bytes read
 * @return 0 on success
 * @return < 0 on error
 */
int cbi_remote_get_board_info(enum cbi_data_tag tag,
                              uint8_t* buffer,
                              uint8_t* buffer_size);

/**
 * Locally simulate the lid switch interrupt
 *
 * Called when the EC notifies us of a change in the lid-open GPIO state.
 *
 * @param is_open True if the lid is open, false otherwise.
 */
void remote_lid_switch_set(bool is_open);

/**
 * Locally simulate the tablet mode switch interrupt
 *
 * Called when the EC notifies us of a change in the tablet mode GPIO state.
 *
 * @param is_360 True if the device is in 360 degree mode, false otherwise
 */
void remote_tablet_switch_set(bool is_360);

/** Reference to the default DSP client device. */
extern const struct device* default_client_device;

struct dsp_client_config {
  struct i2c_dt_spec i2c;
  struct gpio_dt_spec interrupt;
};

struct dsp_client_data {
  struct k_mutex mutex;
  struct k_event response_ready_event;
  struct k_work read_status_work;
  const struct dsp_client_config* config;
  struct gpio_callback gpio_cb;
  int interrupt_config;
  uint32_t pending_response_length;
  cros_dsp_comms_EcService service;
  uint8_t request_buffer[cros_dsp_comms_EcService_size];
  uint8_t response_buffer[CONFIG_PLATFORM_EC_DSP_RESPONSE_BUFFER_SIZE];
};

#ifdef __cplusplus
}
#endif

#endif /* EC_ZEPHYR_DRIVERS_DSP_COMMS_INCLUDE_CROS_DSP_CLIENT_H_ */
