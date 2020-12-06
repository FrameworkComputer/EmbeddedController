/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STM32_FLASH_REGS_H
#define __CROS_EC_STM32_FLASH_REGS_H

#include <stdbool.h>

/**
 * Unlock the flash control register using the unlock sequence.
 *
 * If the flash control register has been disabled since the last reset when
 * this function is called, a bus fault will be generated.
 *
 * See "3.5.1 Unlocking the Flash control register" in RM0402.
 * See "4.9.2  FLASH key register for bank 1" in RM0433.
 */
void unlock_flash_control_register(void);

/**
 * Unlock the flash option bytes register using the unlock sequence.
 *
 * If the flash option bytes register has been disabled since the last reset
 * when this function is called, a bus fault will be generated.
 *
 * See "3.6.2 Programming user option bytes" in RM0402.
 * See "4.9.3 FLASH option key register" in RM0433.
 */
void unlock_flash_option_bytes(void);

/**
 * Lock the flash control register.
 *
 * If the flash control register has been disabled since the last reset when
 * this function is called, a bus fault will be generated.
 *
 * See "3.5.1 Unlocking the Flash control register" in RM0402.
 * See "4.9.4 Flash control register for bank 1" in RM0433.
 */
void lock_flash_control_register(void);

/**
 * Lock the flash option bytes register.
 *
 * If the flash option bytes register has been disabled since the last reset
 * when this function is called, a bus fault will be generated.
 *
 * See "3.6.2 Programming user option bytes" in RM0402.
 * See "4.9.7 FLASH option control register" in RM0433.
 */
void lock_flash_option_bytes(void);

/**
 * Disable the flash option bytes register.
 *
 * This function expects that bus faults have not already been ignored when
 * called.
 *
 * Once this function is called any attempt at accessing the flash option
 * bytes register will generate a bus fault until the next reset.
 *
 * See "3.6.2 Programming user option bytes" in RM0402.
 * See "4.9.7 FLASH option control register" in RM0433.
 */
void disable_flash_option_bytes(void);

/**
 * Disable the flash control register.
 *
 * This function expects that bus faults have not already been ignored when
 * called.
 *
 * Once this function is called any attempt at accessing the flash control
 * register will generate a bus fault until the next reset.
 *
 * See "3.5.1 Unlocking the Flash control register" in RM0402.
 * See "4.9.4 Flash control register for bank 1" in RM0433.
 */
void disable_flash_control_register(void);

/**
 * Check if the flash option bytes are locked.
 *
 * If the flash option bytes register has been disabled since the last reset
 * when this function is called, a bus fault will be generated.

 * See "3.6.2 Programming user option bytes" in RM0402.
 * See "4.9.7 FLASH option control register" in RM0433.
 *
 * @return true if option bytes are locked, false otherwise
 */
bool flash_option_bytes_locked(void);

/**
 * Check if the flash control register is locked.
 *
 * If the flash control register has been disabled since the last reset
 * when this function is called, a bus fault will be generated.
 *
 * See "3.5.1 Unlocking the Flash control register" in RM0402.
 * See "4.9.4 Flash control register for bank 1" in RM0433.
 *
 * @return true if register is locked, false otherwise
 */
bool flash_control_register_locked(void);

#endif /* __CROS_EC_STM32_FLASH_REGS_H */
