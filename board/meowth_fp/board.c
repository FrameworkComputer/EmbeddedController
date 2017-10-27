/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Meowth Fingerprint MCU configuration */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"

#include "gpio_list.h"

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	/* Fingerprint sensor (SCLK at 4Mhz) */
	{ CONFIG_SPI_FP_PORT, 3, GPIO_SPI4_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static void spi_configure(void)
{
	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	/* Set all SPI master signal pins to very high speed: pins E2/4/5/6 */
	STM32_GPIO_OSPEEDR(GPIO_E) |= 0x00003f30;
	/* Enable clocks to SPI4 module (master) */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI4;

	spi_enable(CONFIG_SPI_FP_PORT, 1);
}

/* Initialize board. */
static void board_init(void)
{
	spi_configure();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
