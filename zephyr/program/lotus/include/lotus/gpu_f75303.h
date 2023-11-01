/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPU_F75303_H__
#define __CROS_EC_GPU_F75303_H__

#include "common.h"
#include "config.h"
#include "gpu_configuration.h"

void gpu_f75303_init(struct gpu_cfg_thermal *sensor);

bool gpu_f75303_present(void);
#endif /* __CROS_EC_GPU_F75303_H__ */
