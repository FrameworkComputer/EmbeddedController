/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "cros/dsp/client.h"
#include "cros_board_info.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

int cbi_get_board_info(enum cbi_data_tag tag, uint8_t* buf, uint8_t* size) {
  LOG_DBG("remote_cbi.h::cbi_get_board_info()");
  return cbi_remote_get_board_info(tag, buf, size);
}
