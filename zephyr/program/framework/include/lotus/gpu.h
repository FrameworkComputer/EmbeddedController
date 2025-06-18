/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_GPU_H__
#define __BOARD_GPU_H__

#include <gpu_configuration.h>
#include "common_cpu_power.h"


bool gpu_power_enable(void);

bool gpu_is_working(void);

bool gpu_module_fault(void);

void gpu_fan_control(int enable);

void set_host_dp_ready(int ready);

void update_gpu_ac_power_state(void);

void set_gpu_gpio(enum gpu_gpio_purpose gpu_gpio, int level);

int get_gpu_gpio(enum gpu_gpio_purpose gpu_gpio);

int get_gpu_latch(void);

bool gpu_fan_board_present(void);

void update_gpu_ac_mode_deferred(int times);

/**
 * Updated the GPU PD type when the GPU init/deinit
 *
 * @param type which PD chips on the GPU module
 */
void gpu_update_pd_type(enum gpu_pd chip);

/**
 * Set the PD chip state after gpu configure the GPIO
 * to ensure the PD is powered on.
 */
void gpu_set_pd_state(void);

/**
 * @brief handle GPU PD events
 *
 */

void gpu_update_pd_vdm(int controller, int port, uint8_t *data, int len);

#endif /* __BOARD_GPU_H__ */
