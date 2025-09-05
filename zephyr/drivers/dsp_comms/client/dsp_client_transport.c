/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros/dsp/client.h"

int dsp_client_i2c_write_dt(const struct i2c_dt_spec* spec,
                            const uint8_t* buf,
                            uint32_t num_bytes) {
  int rc;
  uint8_t attempts = 0;

  do {
    if (attempts > 0) {
      k_msleep(CONFIG_PLATFORM_EC_DSP_CLIENT_TX_RETRY_INTERVAL_MS);
    }
    rc = i2c_write_dt(spec, buf, num_bytes);
    attempts++;
  } while (rc < 0 && attempts <= CONFIG_PLATFORM_EC_DSP_CLIENT_TX_RETRY_MAX);
  return rc;
}
