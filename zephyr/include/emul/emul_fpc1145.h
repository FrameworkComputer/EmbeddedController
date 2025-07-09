/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_FPC1145_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_FPC1145_H_

/* FPC1145 example hardware id */
#define FPC1145_HWID 0x140B

/**
 * Set hardware id returned by emulator
 *
 * @param target The target emulator to modify
 * @param hardware_id new hardware id
 */
void fpc1145_set_hwid(const struct emul *target, uint16_t hardware_id);

/**
 * Get low power mode status
 *
 * @param target The target emulator to get status
 */
uint8_t fpc1145_get_low_power_mode(const struct emul *target);

/**
 * Stop handling IRQ gpio
 *
 * @param target The target emulator
 */
void fpc1145_stop_irq(const struct emul *target);

/**
 * Stop SPI transactions
 *
 * @param target The target emulator
 */
void fpc1145_stop_spi(const struct emul *target);

/**
 * Start SPI transactions
 *
 * @param target The target emulator
 */
void fpc1145_start_spi(const struct emul *target);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_FPC1145_H_ */
