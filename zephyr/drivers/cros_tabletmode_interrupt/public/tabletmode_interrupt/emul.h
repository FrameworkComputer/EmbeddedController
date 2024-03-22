/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EC_ZEPHYR_DRIVERS_CROS_TABLETMODE_INTERRUPT_EMUL_H
#define EC_ZEPHYR_DRIVERS_CROS_TABLETMODE_INTERRUPT_EMUL_H

#include "common.h"

/**
 * @brief Override the device_ready state of the interrupt bus
 *
 * This function can only be used in tests.
 *
 * @param is_ready New state of the bus driver's is_ready state.
 */
void tabletmode_interrupt_set_device_ready(bool is_ready);

/**
 * @brief Initialize the tablet mode driver
 *
 * This function is made public for testing.
 *
 * @return 0 on success
 * @return < 0 on error
 */
int tabletmode_init_mode_interrupt(void);

/**
 * @brief Handler for the chipset resume
 *
 * When triggered, it will enable the keyboard scanning based on the lid angle.
 * This function is made public for testing.
 */
void tabletmode_enable_peripherals(void);

/**
 * @brief Handler for the chipset suspend
 *
 * When triggered, it will disable the keyboard scanning if the device is in
 * tablet mode. This function is made public for testing.
 */
void tabletmode_suspend_peripherals(void);

#endif /* EC_ZEPHYR_DRIVERS_CROS_TABLETMODE_INTERRUPT_EMUL_H */
