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
#include "util.h"
#include "board_rw.h"

#ifndef SECTION_IS_RW
#error "This file should only be built for RW."
#endif

/* SPI devices */
struct spi_device_t spi_devices[] = {
	/* Fingerprint sensor (SCLK at 4Mhz) */
	{ .port = CONFIG_SPI_FP_PORT, .div = 3, .gpio_cs = GPIO_SPI4_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/* Allow changing the signal used for S3 sleep depending on the board being
 * used: http://b/179946521.
 */
static int gpio_slp_s3_l = GPIO_PCH_SLP_S3_L;

static void ap_deferred(void)
{
	/*
	 * in S3:   SLP_S3_L is 0 and SLP_S0_L is X.
	 * in S0ix: SLP_S3_L is X and SLP_S0_L is 0.
	 * in S0:   SLP_S3_L is 1 and SLP_S0_L is 1.
	 * in S5/G3, the FP MCU should not be running.
	 */
	int running = gpio_get_level(gpio_slp_s3_l) &&
		      gpio_get_level(GPIO_PCH_SLP_S0_L);

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

static void spi_configure(enum fp_sensor_spi_select spi_select)
{
	if (spi_select == FP_SENSOR_SPI_SELECT_DEVELOPMENT) {
		/* SPI4 master to sensor: PE12/13/14 (CLK/MISO/MOSI) */
		gpio_set_flags_by_mask(GPIO_E, 0x7000, 0);
		gpio_set_alternate_function(GPIO_E, 0x7000, GPIO_ALT_SPI);
	} else {
		gpio_config_module(MODULE_SPI_MASTER, 1);
	}

	/* Set all SPI master signal pins to very high speed: pins E2/4/5/6 */
	STM32_GPIO_OSPEEDR(GPIO_E) |= 0x00003f30;
	/* Enable clocks to SPI4 module (master) */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI4;

	if (spi_select == FP_SENSOR_SPI_SELECT_DEVELOPMENT)
		spi_devices[0].gpio_cs = GPIO_SPI4_ALT_NSS;
	spi_enable(&spi_devices[0], 1);
}

void board_init_rw(void)
{
	enum fp_sensor_spi_select spi_select = get_fp_sensor_spi_select();

	ccprints("FP_SPI_SEL: %s", fp_sensor_spi_select_to_str(spi_select));

	spi_configure(spi_select);

	ccprints("TRANSPORT_SEL: %s",
		 fp_transport_type_to_str(get_fp_transport_type()));

	/* Use SPI select as a proxy for running on the icetower dev board. */
	if (spi_select == FP_SENSOR_SPI_SELECT_DEVELOPMENT)
		gpio_slp_s3_l = GPIO_PCH_SLP_S3_L_ALT;

	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(gpio_slp_s3_l);
	gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);
	/* enable the SPI slave interface if the PCH is up */
	hook_call_deferred(&ap_deferred_data, 0);
}
