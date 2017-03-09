/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "spi.h"
#include "system.h"
#include "registers.h"

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Interrupt line from the fingerprint senser */
void fps_event(enum gpio_signal signal)
{
	/* HACK: Forward interrupt to AP */
	gpio_set_level(GPIO_AP_INT, gpio_get_level(GPIO_FPS_INT));
	CPRINTS("FPS %d\n", gpio_get_level(GPIO_FPS_INT));
}

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

	/* we are ready for host transactions */
	hook_notify(HOOK_CHIPSET_RESUME);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int fp_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_passthru *params = args->params;
	void *out = args->response;
	int rc;
	int ret = EC_RES_SUCCESS;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (params->len > args->params_size +
	    offsetof(struct ec_params_fp_passthru, data) ||
	    params->len > args->response_max)
		return EC_RES_INVALID_PARAM;

	rc = spi_transaction_async(&spi_devices[0], params->data,
				   params->len, out, SPI_READBACK_ALL);
	if (params->flags & EC_FP_FLAG_NOT_COMPLETE)
		rc |= spi_transaction_wait(&spi_devices[0]);
	else
		rc |= spi_transaction_flush(&spi_devices[0]);

	if (rc == EC_ERROR_TIMEOUT)
		ret = EC_RES_TIMEOUT;
	else if (rc)
		ret = EC_RES_ERROR;

	args->response_size = params->len;
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_PASSTHRU, fp_command_passthru, EC_VER_MASK(0));
