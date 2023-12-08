/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_THERMAL_H__
#define __CROS_EC_BOARD_THERMAL_H__

#include "common.h"
#include "config.h"
#include "gpu_configuration.h"

void fan_configure_gpu(struct gpu_cfg_fan *fan);

#endif /* __CROS_EC_BOARD_THERMAL_H__ */
