/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_WRITE_PROTECT_H
#define __CROS_EC_ZEPHYR_WRITE_PROTECT_H

#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "wp_external.h"

#include <stdbool.h>

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
static inline bool write_protect_is_asserted(void)
{
#ifdef CONFIG_WP_ALWAYS
	return true;
#elif CONFIG_PLATFORM_EC_WP_EXTERNAL
	return write_protect_is_asserted_external();
#else
	/*
	 * Read write protect GPIO. Return the protected state (more safe) in
	 * case of any error.
	 */
	return gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_wp)) != 0;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_WRITE_PROTECT_H */
