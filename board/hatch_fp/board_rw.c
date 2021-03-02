/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "fpsensor.h"
#include "fpsensor_detect.h"
#include "fpsensor_elan.h"
#include "fpsensor_fpc.h"
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
static bool broken_slp_s0;

static void ap_deferred(void)
{
	/*
	 * in S3:   SLP_S3_L is 0 and SLP_S0_L is X.
	 * in S0ix: SLP_S3_L is 1 and SLP_S0_L is 0.
	 * in S0:   SLP_S3_L is 1 and SLP_S0_L is 1.
	 * in S5/G3, the FP MCU should not be running.
	 */
	int running = gpio_get_level(GPIO_PCH_SLP_S3_L) &&
		      (gpio_get_level(GPIO_PCH_SLP_S0_L) || broken_slp_s0);

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
	gpio_config_module(MODULE_SPI_MASTER, 1);

	/* Set all SPI master signal pins to very high speed: B12/13/14/15 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module (master) */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	spi_enable(&spi_devices[0], 1);
}

void board_init_rw(void)
{
	enum fp_transport_type ret_transport = get_fp_transport_type();
	enum fp_sensor_type sensor_type = get_fp_sensor_type();

	if (sensor_type == FP_SENSOR_TYPE_ELAN) {
		if (IS_ENABLED(CONFIG_FP_SENSOR_ELAN80) ||
		    IS_ENABLED(CONFIG_FP_SENSOR_ELAN515)) {
			fp_driver = &fp_driver_elan;
		}
	} else if (sensor_type == FP_SENSOR_TYPE_FPC) {
		if (IS_ENABLED(CONFIG_FP_SENSOR_FPC1025) ||
		    IS_ENABLED(CONFIG_FP_SENSOR_FPC1035) ||
		    IS_ENABLED(CONFIG_FP_SENSOR_FPC1145)) {
			fp_driver = &fp_driver_fpc;
		}
	}

	if (fp_driver == NULL) {
		ccprints("Failed to get sensor type!");
	}

	if (ret_transport == FP_TRANSPORT_TYPE_UART) {
		/*
		 * The Zork variants currently have a broken SLP_S0_L signal
		 * (stuck to 0 in S0). For now, unconditionally ignore it here
		 * as they are the only UART users and the AP has no S0ix state.
		 * TODO(b/174695987) once the RW AP firmware has been updated
		 * on all those machines, remove this workaround.
		 */
		broken_slp_s0 = true;
	}

	/* Configure and enable SPI as master for FP sensor */
	configure_fp_sensor_spi();

	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
	gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);

	/* enable the SPI slave interface if the PCH is up */
	hook_call_deferred(&ap_deferred_data, 0);
}
