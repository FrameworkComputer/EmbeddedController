/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_WRITE_PROTECT_H
#define __CROS_EC_ZEPHYR_WRITE_PROTECT_H

#include "gpio/gpio_int.h"
#include "gpio_signal.h"

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check the WP state. The function depends on the alias 'gpio_wp'. It is used
 * to replace the enum-name.
 *
 * @return true if the WP is active, false otherwise.
 */
static inline int write_protect_is_asserted(void)
{
#ifdef CONFIG_WP_ALWAYS
	return true;
#else
	return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_wp));
#endif
}

/**
 * Enable interrupt for WP pin. The interrupt itself has to be defined in a node
 * with compatible = "cros-ec,gpio-interrupts" and pointed by the alias int_wp.
 *
 * @return 0 if success
 */
static inline int write_protect_enable_interrupt(void)
{
#if DT_NODE_EXISTS(DT_ALIAS(int_wp))
	return gpio_enable_dt_interrupt(GPIO_INT_FROM_NODE(DT_ALIAS(int_wp)));
#else
	return -1;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_WRITE_PROTECT_H */
