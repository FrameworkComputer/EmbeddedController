/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_I2C_BITBANG_H
#define __CROS_EC_I2C_BITBANG_H

#include "i2c.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct i2c_drv bitbang_drv;

extern const struct i2c_port_t i2c_bitbang_ports[];
extern const unsigned int i2c_bitbang_ports_used;

/**
 * Enable I2C raw mode for the ports which need pre-task
 * I2C transactions in bitbang mode.
 *
 * @param enable Enable/disable the I2C raw mode
 */
void enable_i2c_raw_mode(bool enable);

/**
 * Board level function to initialize I2C peripherals before task starts.
 *
 * Note: This requires CONFIG_I2C_BITBANG to be enabled, as the task event
 * based I2C transactions can only be done in bitbang mode if accessed pre-task.
 *
 * Example: I/O expanders can be initialized to utilize GPIOs earlier
 * than the HOOK task starts.
 */
__override_proto void board_pre_task_i2c_peripheral_init(void);

/* expose static functions for testing */
#ifdef TEST_BUILD
int bitbang_start_cond(const struct i2c_port_t *i2c_port);
void bitbang_stop_cond(const struct i2c_port_t *i2c_port);
int bitbang_write_byte(const struct i2c_port_t *i2c_port, uint8_t byte);
void bitbang_set_started(int val);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_I2C_BITBANG_H */
