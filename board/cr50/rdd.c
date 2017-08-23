/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "device_state.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rbox.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "uartn.h"
#include "usb_api.h"
#include "usb_i2c.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

int rdd_is_connected(void)
{
	return ccd_get_mode() == CCD_MODE_ENABLED;
}

/* If the UART TX is connected the pinmux select will have a non-zero value */
int uart_tx_is_connected(int uart)
{
	if (uart == UART_AP)
		return GREAD(PINMUX, DIOA7_SEL);
	return GREAD(PINMUX, DIOB5_SEL);
}

/**
 * Connect the UART pin to the given signal
 *
 * @param uart		the uart peripheral number
 * @param signal	the pinmux selector value for the gpio or peripheral
 *			function. 0 to disable the output.
 */
static void uart_select_tx(int uart, int signal)
{
	if (uart == UART_AP) {
		GWRITE(PINMUX, DIOA7_SEL, signal);
	} else {
		GWRITE(PINMUX, DIOB5_SEL, signal);

		/* Remove the pulldown when we are driving the signal */
		GWRITE_FIELD(PINMUX, DIOB5_CTL, PD, signal ? 0 : 1);
	}
}

int servo_is_connected(void)
{
	return device_get_state(DEVICE_SERVO) == DEVICE_STATE_ON;
}

void uartn_tx_connect(int uart)
{
	/*
	 * Don't drive TX unless the debug cable is connected (we have
	 * something to transmit) and servo is disconnected (we won't be
	 * drive-fighting with servo).
	 */
	if (servo_is_connected() || !rdd_is_connected())
		return;

	if (uart == UART_AP) {
		if (!ccd_is_cap_enabled(CCD_CAP_AP_RX_CR50_TX))
			return;

		if (!ap_is_on())
			return;

		uart_select_tx(UART_AP, GC_PINMUX_UART1_TX_SEL);
	} else {
		if (!ccd_is_cap_enabled(CCD_CAP_EC_RX_CR50_TX))
			return;

		if (!ec_is_on())
			return;

		uart_select_tx(UART_EC, GC_PINMUX_UART2_TX_SEL);
	}
}

void uartn_tx_disconnect(int uart)
{
	/* Disconnect the TX pin from UART peripheral */
	uart_select_tx(uart, 0);
}

static void configure_ccd(int enable)
{
	if (enable) {
		if (rdd_is_connected())
			return;

		/* Enable CCD */
		ccd_set_mode(CCD_MODE_ENABLED);

		/* Attempt to connect UART TX */
		uartn_tx_connect(UART_AP);
		uartn_tx_connect(UART_EC);

		/* Turn on 3.3V rail used for INAs and initialize I2CM module */
		usb_i2c_board_enable();
	} else {
		/* Disconnect from AP and EC UART TX peripheral from gpios */
		uartn_tx_disconnect(UART_EC);
		uartn_tx_disconnect(UART_AP);

		/* Disable CCD */
		ccd_set_mode(CCD_MODE_DISABLED);

		/* Turn off 3.3V rail to INAs and disconnect I2CM module */
		usb_i2c_board_disable();
	}
	CPRINTS("CCD is now %sabled.", enable ? "en" : "dis");
}

static void rdd_check_pin(void)
{
	/* The CCD mode pin is active low. */
	int enable = !gpio_get_level(GPIO_CCD_MODE_L);

	if (enable == rdd_is_connected())
		return;

	configure_ccd(enable);
}
DECLARE_HOOK(HOOK_SECOND, rdd_check_pin, HOOK_PRIO_DEFAULT);

static void rdd_ccd_change_hook(void)
{
	if (uart_tx_is_connected(UART_AP) &&
	    !ccd_is_cap_enabled(CCD_CAP_AP_RX_CR50_TX)) {
		/* Transmitting to AP, but no longer allowed */
		uartn_tx_disconnect(UART_AP);
	} else if (!uart_tx_is_connected(UART_AP) &&
		   ccd_is_cap_enabled(CCD_CAP_AP_RX_CR50_TX)) {
		/* Not transmitting to AP, but allowed now */
		uartn_tx_connect(UART_AP);
	}

	if (uart_tx_is_connected(UART_EC) &&
	    !ccd_is_cap_enabled(CCD_CAP_EC_RX_CR50_TX)) {
		/* Transmitting to EC, but no longer allowed */
		uartn_tx_disconnect(UART_EC);
	} else if (!uart_tx_is_connected(UART_EC) &&
		   ccd_is_cap_enabled(CCD_CAP_EC_RX_CR50_TX)) {
		/* Not transmitting to EC, but allowed now */
		uartn_tx_connect(UART_EC);
	}
}
DECLARE_HOOK(HOOK_CCD_CHANGE, rdd_ccd_change_hook, HOOK_PRIO_DEFAULT);

static int command_ccd(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!parse_bool(argv[argc - 1], &val))
			return argc == 2 ? EC_ERROR_PARAM1 : EC_ERROR_PARAM2;

		if (!strcasecmp("uart", argv[1])) {
			if (val)
				uartn_tx_connect(UART_EC);
			else
				uartn_tx_disconnect(UART_EC);
		} else if (!strcasecmp("i2c", argv[1])) {
			if (val)
				usb_i2c_board_enable();
			else
				usb_i2c_board_disable();
		} else if (!strcasecmp("keepalive", argv[1])) {
			force_rdd_detect(val);
			if (val) {
				ccprintf("Warning CCD will remain "
					 "enabled until it is "
					 "explicitly disabled.\n");
			}
		} else
			return EC_ERROR_PARAM1;
	}

	print_ap_state();
	print_ec_state();
	print_rdd_state();

	ccprintf("CCD:     %s\n", rdd_is_connected() ? "enabled" : "disabled");
	ccprintf("AP UART: %s\n",
		 uartn_is_enabled(UART_AP) ?
		 uart_tx_is_connected(UART_AP) ? "RX+TX" : "RX" : "disabled");
	ccprintf("EC UART: %s\n",
		 uartn_is_enabled(UART_EC) ?
		 uart_tx_is_connected(UART_EC) ? "RX+TX" : "RX" : "disabled");
	ccprintf("I2C:     %s\n",
		 usb_i2c_board_is_enabled() ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccd, command_ccd,
			"[uart|i2c|keepalive] [<BOOLEAN>]",
			"Get/set the case closed debug state");

static int command_sys_rst(int argc, char **argv)
{
	int val;
	char *e;
	int ms = 20;

	if (argc > 1) {
		if (!ccd_is_cap_enabled(CCD_CAP_REBOOT_EC_AP))
			return EC_ERROR_ACCESS_DENIED;

		if (!strcasecmp("pulse", argv[1])) {
			if (argc == 3) {
				ms = strtoi(argv[2], &e, 0);
				if (*e)
					return EC_ERROR_PARAM2;
			}
			ccprintf("Pulsing AP reset for %dms\n", ms);
			assert_sys_rst();
			msleep(ms);
			deassert_sys_rst();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_sys_rst();
			else
				deassert_sys_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("SYS_RST_L is %s\n", is_sys_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;

}
DECLARE_SAFE_CONSOLE_COMMAND(sysrst, command_sys_rst,
	"[pulse [time] | <BOOLEAN>]",
	"Assert/deassert SYS_RST_L to reset the AP");

static int command_ec_rst(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!ccd_is_cap_enabled(CCD_CAP_REBOOT_EC_AP))
			return EC_ERROR_ACCESS_DENIED;

		if (!strcasecmp("pulse", argv[1])) {
			ccprintf("Pulsing EC reset\n");
			assert_ec_rst();
			usleep(200);
			deassert_ec_rst();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_ec_rst();
			else
				deassert_ec_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("EC_RST_L is %s\n", is_ec_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ecrst, command_ec_rst,
	"[pulse | <BOOLEAN>]",
	"Assert/deassert EC_RST_L to reset the EC (and AP)");

static int command_powerbtn(int argc, char **argv)
{
	char *e;
	int ms = 200;

	if (argc > 1) {
		if (!strcasecmp("pulse", argv[1])) {
			if (argc == 3) {
				ms = strtoi(argv[2], &e, 0);
				if (*e)
					return EC_ERROR_PARAM2;
			}

			ccprintf("Force %dms power button press\n", ms);

			rbox_powerbtn_press();
			msleep(ms);
			rbox_powerbtn_release();
		} else if (!strcasecmp("press", argv[1])) {
			rbox_powerbtn_press();
		} else if (!strcasecmp("release", argv[1])) {
			rbox_powerbtn_release();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("powerbtn: %s\n",
		 rbox_powerbtn_override_is_enabled() ? "forced press" :
		 rbox_powerbtn_is_pressed() ? "pressed\n" : "released\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn,
			"[pulse [ms] | press | release]",
			"get/set the state of the power button");
