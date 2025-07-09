/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include "cros/dsp/client.h"
#include "hooks.h"
#include "tablet_mode.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

static int is_board_sensor_at_360;

void remote_tablet_switch_set(bool is_360) {
  LOG_DBG("remote_tablet_switch_set(%d)", is_360);
  int new_value = is_360 ? 1 : 0;
  if (is_board_sensor_at_360 == new_value) {
    return;
  }
  is_board_sensor_at_360 = new_value;
}

int board_sensor_at_360() { return is_board_sensor_at_360; }
