/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_GPU_H__
#define __BOARD_GPU_H__

/* gpu install stable */
bool gpu_present(void);

bool gpu_power_enable(void);

bool gpu_module_fault(void);

void gpu_fan_control(int enable);

void set_host_dp_ready(int ready);

void update_gpu_ac_power_state(void);

#endif /* __BOARD_GPU_H__ */
