/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger/chg_isl95522.h"
#include "emul/emul_common_i2c.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ISL95522_CHG_COMPAT

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
