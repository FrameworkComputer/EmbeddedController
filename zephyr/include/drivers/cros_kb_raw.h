/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Chrome OS-specific API for raw keyboard access
 * This exists only support the interface expected by the Chrome OS EC. It seems
 * better to implement this so we can make use of most of the existing code in
 * its keyboard_scan.c file and thus make sure we operate the same way.
 *
 * It provides raw access to keyboard GPIOs.
 *
 * The keyboard matrix is read (by the caller, keyboard_scan.c in ECOS) by
 * driving output signals on the column lines and reading the row lines.
 *
 * This API and any drivers should be removed once we can safely move to using
 * the Zephyr kscan API.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 27

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_KB_RAW_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_KB_RAW_H_

#include "gpio_signal.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/*
 * When CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED is enabled, the keyboard
 * driver must drive the column 2 output to the opposite state. If the keyboard
 * driver doesn't support push-pull operation, then the pin is set using the
 * GPIO module. Use the presence of the alias node "gpio-kbd-kso2" to determine
 * when this code is needed.
 */
#define KBD_KSO2_NODE DT_ALIAS(gpio_kbd_kso2)

/**
 * @brief CROS Keyboard Raw Driver APIs
 * @defgroup cros_kb_raw_interface CROS Keyboard Raw Driver APIs
 * @ingroup io_interfaces
 * @{
 */

/**
 * @cond INTERNAL_HIDDEN
 *
 * cros keyboard raw driver API definition and system call entry points
 *
 * (Internal use only.)
 */
typedef int (*cros_kb_raw_api_init)(const struct device *dev);

typedef int (*cros_kb_raw_api_drive_column)(const struct device *dev, int col);

typedef int (*cros_kb_raw_api_read_rows)(const struct device *dev);

typedef int (*cros_kb_raw_api_enable_interrupt)(const struct device *dev,
						int enable);

typedef int (*cros_kb_raw_api_config_alt)(const struct device *dev,
					  bool enable);

__subsystem struct cros_kb_raw_driver_api {
	cros_kb_raw_api_init init;
	cros_kb_raw_api_drive_column drive_colum;
	cros_kb_raw_api_read_rows read_rows;
	cros_kb_raw_api_enable_interrupt enable_interrupt;
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
	cros_kb_raw_api_config_alt config_alt;
#endif
};

/**
 * @endcond
 */

/**
 * @brief Initialize the raw keyboard interface.
 *
 * Must be called before any other functions in this interface.
 *
 * @param dev Pointer to the device structure for the keyboard driver instance.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_kb_raw_init(const struct device *dev);

static inline int z_impl_cros_kb_raw_init(const struct device *dev)
{
	const struct cros_kb_raw_driver_api *api =
		(const struct cros_kb_raw_driver_api *)dev->api;

	if (!api->init) {
		return -ENOTSUP;
	}

	return api->init(dev);
}

/**
 * @brief Drive the specified column low.
 *
 * Other columns are tristated.  See enum keyboard_column_index for special
 * values for <col>.
 *
 * @param dev Pointer to the device structure for the keyboard driver instance.
 * @param col Specified column is driven to low.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_kb_raw_drive_column(const struct device *dev, int col);
static inline int z_impl_cros_kb_raw_drive_column(const struct device *dev,
						  int col)
{
	const struct cros_kb_raw_driver_api *api =
		(const struct cros_kb_raw_driver_api *)dev->api;

	if (!api->drive_colum) {
		return -ENOTSUP;
	}

	return api->drive_colum(dev, col);
}

/**
 * @brief Read raw row state.
 *
 * Bits are 1 if signal is present, 0 if not present.
 *
 * @param dev Pointer to the device structure for the keyboard driver instance.
 *
 * @return current raw row state value.
 */
__syscall int cros_kb_raw_read_rows(const struct device *dev);
static inline int z_impl_cros_kb_raw_read_rows(const struct device *dev)
{
	const struct cros_kb_raw_driver_api *api =
		(const struct cros_kb_raw_driver_api *)dev->api;

	if (!api->read_rows) {
		return 0;
	}

	return api->read_rows(dev);
}

/**
 * @brief Enable or disable keyboard interrupts.
 *
 * Enabling interrupts will clear any pending interrupt bits.  To avoid missing
 * any interrupts that occur between the end of scanning and then, you should
 * call cros_kb_raw_read_rows() after this.  If it returns non-zero, disable
 * interrupts and go back to polling mode instead of waiting for an interrupt.
 *
 * @param dev Pointer to the device structure for the keyboard driver instance.
 * @param enable If 1, enable keyboard interrupt. Otherwise, disable it.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_kb_raw_enable_interrupt(const struct device *dev,
					   int enable);

static inline int z_impl_cros_kb_raw_enable_interrupt(const struct device *dev,
						      int enable)
{
	const struct cros_kb_raw_driver_api *api =
		(const struct cros_kb_raw_driver_api *)dev->api;

	if (!api->enable_interrupt) {
		return -ENOTSUP;
	}

	return api->enable_interrupt(dev, enable);
}

/**
 * @brief Enable or disable keyboard alternative function.
 *
 * Enabling alternative function.
 *
 * @param dev Pointer to the device structure for the keyboard driver instance.
 * @param enable If 1, enable keyboard function. Otherwise, disable it (as
 * GPIO).
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
__syscall int cros_kb_raw_config_alt(const struct device *dev, bool enable);

static inline int z_impl_cros_kb_raw_config_alt(const struct device *dev,
						bool enable)
{
	const struct cros_kb_raw_driver_api *api =
		(const struct cros_kb_raw_driver_api *)dev->api;

	if (!api->config_alt) {
		return -ENOTSUP;
	}

	return api->config_alt(dev, enable);
}
#endif

/**
 * @brief Set the logical level of the keyboard column 2 output.
 *
 * When CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED is enabled, the column 2
 * output connects to the Google Security Chip and must use push-pull operation.
 * Typically the column 2 signal is also inverted in this configuration so the
 * board devicetree should set the GPIO_ACTIVE_LOW flag on GPIO pointed to by
 * gpio-kbd-kso2.
 *
 * @param value Logical level to set to the pin
 */
static inline void cros_kb_raw_set_col2(int level)
{
#if defined CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED && \
	DT_NODE_EXISTS(KBD_KSO2_NODE)
	const struct gpio_dt_spec *kbd_dt_spec =
		GPIO_DT_FROM_NODE(KBD_KSO2_NODE);

	gpio_pin_set(kbd_dt_spec->port, kbd_dt_spec->pin, level);
#endif
}

/**
 * @}
 */
#include <zephyr/syscalls/cros_kb_raw.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_KB_RAW_H_ */
