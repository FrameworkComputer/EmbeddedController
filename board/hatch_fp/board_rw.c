/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "fpsensor_detect.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "usart_host_command.h"
#include "util.h"

#ifndef SECTION_IS_RW
#error "This file should only be built for RW."
#endif

/*
 * Some platforms have a broken SLP_S0_L signal (stuck to 0 in S0)
 * if set, ignore it and only uses SLP_S3_L for the AP state.
 */
static bool broken_slp;

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
	 * in S0ix: SLP_ALT_L is 1 and SLP_L is 0.
	 * in S0:   SLP_ALT_L is 1 and SLP_L is 1.
	 * in S5/G3, the FP MCU should not be running.
	 */
	int running = gpio_get_level(GPIO_SLP_ALT_L) &&
		      (gpio_get_level(GPIO_SLP_L) || broken_slp);

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
void slp_event(enum gpio_signal signal)
{
	hook_call_deferred(&ap_deferred_data, 0);
}

/* SPI devices */
struct spi_device_t spi_devices[] = {
	/* Fingerprint sensor (SCLK at 4Mhz) */
	{ .port = CONFIG_SPI_FP_PORT, .div = 3, .gpio_cs = GPIO_SPI2_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static void configure_fp_sensor_spi(void)
{
	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);

	/* Set all SPI master signal pins to very high speed: B12/13/14/15 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module (master) */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	spi_enable(&spi_devices[0], 1);
}

void board_init_rw(void)
{
	enum fp_transport_type ret_transport = get_fp_transport_type();

	/*
	 * FP_RST_ODL pin is defined in gpio_rw.inc (with GPIO_OUT_HIGH
	 * flag) but not in gpio.inc, so RO leaves this pin set to 0 (reset
	 * default), but RW doesn't initialize this pin to 1 because sysjump
	 * to RW is a warm reset (see gpio_pre_init() in chip/stm32/gpio.c).
	 * Explicitly reset FP_RST_ODL pin to default value.
	 */
	gpio_reset(GPIO_FP_RST_ODL);

	if (ret_transport == FP_TRANSPORT_TYPE_UART) {
		/*
		 * The Zork variants currently have a broken SLP_S0_L signal
		 * (stuck to 0 in S0). For now, unconditionally ignore it here
		 * as they are the only UART users and the AP has no S0ix state.
		 * TODO(b/174695987) once the RW AP firmware has been updated
		 * on all those machines, remove this workaround.
		 */
		broken_slp = true;
	}

	/* Configure and enable SPI as master for FP sensor */
	configure_fp_sensor_spi();

	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(GPIO_SLP_ALT_L);
	gpio_enable_interrupt(GPIO_SLP_L);

	/*
	 * Enable the SPI slave interface if the PCH is up.
	 * Do not use hook_call_deferred(), because ap_deferred() will be
	 * called after tasks with priority higher than HOOK task (very late).
	 */
	ap_deferred();
}
