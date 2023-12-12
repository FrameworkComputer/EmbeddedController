/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "fpsensor/fpsensor_detect.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "util.h"

/**
 * Disable restricted commands when the system is locked.
 *
 * @see console.h system.c
 */
int console_is_restricted(void)
{
	return system_is_locked();
}

static void ap_deferred(void)
{
	/*
	 * Behavior:
	 * AP Active  (ex. Intel S0):   SLP_L is 1
	 * AP Suspend (ex. Intel S0ix): SLP_L is 0
	 * The alternative SLP_ALT_L should be pulled high at all the times.
	 *
	 * Legacy Intel behavior:
	 * in S3:   SLP_ALT_L is 0 and SLP_L is X.
	 * in S0ix: SLP_ALT_L is X and SLP_L is 0.
	 * in S0:   SLP_ALT_L is 1 and SLP_L is 1.
	 * in S5/G3, the FP MCU should not be running.
	 */
	int running = gpio_get_level(GPIO_SLP_ALT_L) &&
		      gpio_get_level(GPIO_SLP_L);

	if (running) { /* S0 */
		disable_sleep(SLEEP_MASK_AP_RUN);
		hook_notify(HOOK_CHIPSET_RESUME);
	} else { /* S0ix/S3 */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		enable_sleep(SLEEP_MASK_AP_RUN);
	}
}
DECLARE_DEFERRED(ap_deferred);

/* PCH power state changes */
static void slp_event(enum gpio_signal signal)
{
	hook_call_deferred(&ap_deferred_data, 0);
}

#ifndef HAS_TASK_FPSENSOR
void fps_event(enum gpio_signal signal)
{
}
#endif

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	/* Fingerprint sensor (SCLK at 4Mhz) */
	{ .port = CONFIG_SPI_FP_PORT, .div = 3, .gpio_cs = GPIO_SPI4_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static void spi_configure(void)
{
	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);
	/* Set all SPI controller signal pins to very high speed:
	 * pins E2/4/5/6
	 */
	STM32_GPIO_OSPEEDR(GPIO_E) |= 0x00003f30;
	/* Enable clocks to SPI4 module (master) */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI4;

	spi_enable(&spi_devices[0], 1);
}

/* Initialize board. */
static void board_init(void)
{
	spi_configure();

	ccprints("TRANSPORT_SEL: %s",
		 fp_transport_type_to_str(get_fp_transport_type()));

	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(GPIO_SLP_ALT_L);
	gpio_enable_interrupt(GPIO_SLP_L);

	/*
	 * Enable the SPI peripheral interface if the PCH is up.
	 * Do not use hook_call_deferred(), because ap_deferred() will be
	 * called after tasks with priority higher than HOOK task (very late).
	 */
	ap_deferred();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
