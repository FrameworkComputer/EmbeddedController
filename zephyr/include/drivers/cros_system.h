/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Public API for cros system
 *
 * Each EC chip type implements the functions prototyped below
 * directly.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_SYSTEM_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_SYSTEM_H_

/**
 * @brief cros system Interface
 * @defgroup cros_system_interface cros system Interface
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/kernel.h>

/**
 * @brief system_reset_cause enum
 * Identify the reset cause.
 */
enum system_reset_cause {
	/* the reset is triggered by VCC power-up */
	POWERUP = 0,
	/* the reset is triggered by external VCC1 reset pin */
	VCC1_RST_PIN = 1,
	/* the reset is triggered by ICE debug reset request */
	DEBUG_RST = 2,
	/* the reset is triggered by watchdog */
	WATCHDOG_RST = 3,
	/* unknown reset type */
	UNKNOWN_RST,
};

enum hibernate_wake_source {
	/* The wake source is AC */
	WAKE_SOURCE_ACOK = 0,
	/* The wake source is Lid open */
	WAKE_SOURCE_LID_OPEN = 1,
	/* The wake source is Power button */
	WAKE_SOURCE_PWR_BTN = 2,
	/* Unknown wake source */
	WAKE_SOURCE_UNKNOWN,
};

/**
 * @brief Get a node from path '/hibernate_wakeup_pins' which has a property
 *        'wakeup-pins' contains GPIO list for hibernate wake-up
 *
 * @return node identifier with that path.
 */
#define SYSTEM_DT_NODE_HIBERNATE_CONFIG DT_INST(0, cros_ec_hibernate_wake_pins)

/**
 * @brief Get the chip-reset cause
 *
 * @retval non-negative if successful.
 * @retval Negative errno code if failure.
 */
int cros_system_get_reset_cause(void);

/**
 * @brief reset the soc
 *
 * @retval no return if successful.
 * @retval Negative errno code if failure.
 */
int cros_system_soc_reset(void);

/**
 * @brief put the EC in hibernate (lowest EC power state).
 *
 * @param seconds Number of seconds before EC enters hibernate state.
 * @param microseconds Number of micro-secs before EC enters hibernate state.
 *
 * @retval no return if successful.
 * @retval Negative errno code if failure.
 */
int cros_system_hibernate(uint32_t seconds, uint32_t microseconds);

/**
 * @brief Get the chip vendor.
 *
 * @retval Chip vendor string if successful.
 * @retval Null string if failure.
 */
const char *cros_system_chip_vendor(void);

/**
 * @brief Get the chip name.
 *
 * @retval Chip name string if successful.
 * @retval Null string if failure.
 */
const char *cros_system_chip_name(void);

/**
 * @brief Get the chip revision.
 *
 * @retval Chip revision string if successful.
 * @retval Null string if failure.
 */
const char *cros_system_chip_revision(void);

/**
 * @brief Get total number of ticks spent in deep sleep.
 *
 * @retval Number of ticks spent in deep sleep.
 */
uint64_t cros_system_deep_sleep_ticks(void);

/**
 * @brief Get hibernate wake source.
 *
 * @param source Pointer to the variable where the hibernate wake source will be
 * stored.
 * @retval 0 if successful.
 * @retval Negative errno code if failure.
 */
int cros_system_get_hibernate_wake_source(enum hibernate_wake_source *source);

/**
 * @}
 */
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_SYSTEM_H_ */
