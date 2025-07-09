/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>

#include "cros/dsp/client.h"
#include "hooks.h"
#include "lid_switch.h"

LOG_MODULE_DECLARE(dsp_client, CONFIG_DSP_COMMS_LOG_LEVEL);

static bool is_lid_switch_open;

void remote_lid_switch_set(bool is_open) {
  LOG_DBG("remote_lid_switch_set(%d)", is_open);
  if (is_lid_switch_open == is_open) {
    return;
  }
  is_lid_switch_open = is_open;
  hook_notify(HOOK_LID_CHANGE);
}

int lid_is_open() { return is_lid_switch_open; }
