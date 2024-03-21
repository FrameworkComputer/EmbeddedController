/* Copyright 2024 The ChromiumOS Authors
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

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

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

static void board_init_transport(void)
{
	enum fp_transport_type ret_transport = get_fp_transport_type();

	ccprints("TRANSPORT_SEL: %s", fp_transport_type_to_str(ret_transport));

	/* Initialize transport based on bootstrap */
	switch (ret_transport) {
	case FP_TRANSPORT_TYPE_UART:
		/*
		 * The Zork variants currently have a broken SLP_S0_L signal
		 * (stuck to 0 in S0). For now, unconditionally ignore it here
		 * as they are the only UART users and the AP has no S0ix state.
		 * TODO(b/174695987) once the RW AP firmware has been updated
		 * on all those machines, remove this workaround.
		 */
		broken_slp = true;

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
	/* Run until the first S3 entry */
	disable_sleep(SLEEP_MASK_AP_RUN);

	board_init_transport();

	/* Enable interrupt on PCH power signals */
	gpio_enable_interrupt(GPIO_SLP_ALT_L);
	gpio_enable_interrupt(GPIO_SLP_L);

	if (IS_ENABLED(SECTION_IS_RW))
		board_init_rw();

	/*
	 * Enable the SPI slave interface if the PCH is up.
	 * Do not use hook_call_deferred(), because ap_deferred() will be
	 * called after tasks with priority higher than HOOK task (very late).
	 */
	ap_deferred();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
