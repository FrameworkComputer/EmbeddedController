/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "registers.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

static void ap_deferred(void)
{
	if (gpio_get_level(GPIO_SLP_S3_L)) {
		/* S0 */
		gpio_set_flags(GPIO_EC_INT_L,   GPIO_ODR_HIGH | GPIO_PULL_UP);
		hook_notify(HOOK_CHIPSET_RESUME);
	} else {
		/* S3 */
		gpio_set_flags(GPIO_EC_INT_L,   GPIO_INPUT);
		hook_notify(HOOK_CHIPSET_SUSPEND);
	}
}
DECLARE_DEFERRED(ap_deferred);

void slp_event(enum gpio_signal signal)
{
	hook_call_deferred(&ap_deferred_data, 0);
}

#include "gpio_list.h"

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	/* Fingerprint sensor */
	{ CONFIG_SPI_FP_PORT, 1, GPIO_SPI3_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/* Initialize board-specific configuraiton */
static void board_init(void)
{
	/* Set all SPI master signal pins to very high speed: pins B3/B4/B5 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00000fc0;
	/* Enable clocks to SPI3 module (master) */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI3;

	/* Enable interrupt on SLP_S3_L */
	gpio_enable_interrupt(GPIO_SLP_S3_L);
	/* enable the SPI slave interface if the PCH is up */
	hook_call_deferred(&ap_deferred_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
