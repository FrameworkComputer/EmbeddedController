/* Copyright 2019 The Chromium OS Authors. All rights reserved.
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

/**
 * Disable restricted commands when the system is locked.
 *
 * @see console.h system.c
 */
int console_is_restricted(void)
{
	return system_is_locked();
}

#include "gpio_list.h"

static void board_init_transport(void)
{
	enum fp_transport_type ret_transport = get_fp_transport_type();

	ccprints("TRANSPORT_SEL: %s", fp_transport_type_to_str(ret_transport));

	/* Initialize transport based on bootstrap */
	switch (ret_transport) {
	case FP_TRANSPORT_TYPE_UART:
		/* Check if CONFIG_USART_HOST_COMMAND is enabled. */
		if (IS_ENABLED(CONFIG_USART_HOST_COMMAND))
			usart_host_command_init();
		else
			ccprints("ERROR: UART not supported in fw build.");

		/* Disable SPI interrupt to disable SPI transport layer */
		gpio_disable_interrupt(GPIO_SPI1_NSS);
		break;

	case FP_TRANSPORT_TYPE_SPI:
		/* SPI transport is enabled. SPI1_NSS interrupt will process
		 * incoming request/
		 */
		break;
	default:
		ccprints("ERROR: Selected transport is not valid.");
	}

	ccprints("TRANSPORT_SEL: %s",
		 fp_transport_type_to_str(get_fp_transport_type()));
}

/* Initialize board. */
static void board_init(void)
{
	/* Run until the first S3 entry.
	 * No suspend-based power management in RO.
	 */
	disable_sleep(SLEEP_MASK_AP_RUN);
	hook_notify(HOOK_CHIPSET_RESUME);
	board_init_transport();
	if (IS_ENABLED(SECTION_IS_RW))
		board_init_rw();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
