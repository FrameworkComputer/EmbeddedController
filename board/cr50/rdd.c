/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rbox.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "uartn.h"
#include "usb_api.h"
#include "usb_console.h"
#include "usb_i2c.h"
#include "usb_spi.h"

/* Include the dazzlingly complex macro to instantiate the USB SPI config */
USB_SPI_CONFIG(ccd_usb_spi, USB_IFACE_SPI, USB_EP_SPI);

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

int ccd_ext_is_enabled(void)
{
	return state == DEVICE_STATE_CONNECTED;
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

void uartn_tx_connect(int uart)
{
	/*
	 * Don't drive TX unless the debug cable is connected (we have
	 * something to transmit) and servo is disconnected (we won't be
	 * drive-fighting with servo).
	 */
	if (servo_is_connected() || !ccd_ext_is_enabled())
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

static void rdd_check_pin(void)
{
	/* The CCD mode pin is active low. */
	int enable = !gpio_get_level(GPIO_CCD_MODE_L);

	if (enable == ccd_ext_is_enabled())
		return;

	if (enable) {
		/*
		 * If we're not disconnected, release USB to ensure it's in a
		 * good state before we usb_init().  This matches what
		 * common/case_closed_debug.c does.
		 *
		 * Not sure exactly why this is necessary.  It could be because
		 * that also has CCD_MODE_PARTIAL, and the only way to go
		 * cleanly between ENABLED and PARTIAL is to disable things and
		 * then re-enable only what's needed?
		 */
		if (state != DEVICE_STATE_DISCONNECTED)
			usb_release();

		CPRINTS("CCD EXT enable");
		state = DEVICE_STATE_CONNECTED;

		usb_console_enable(1, 0);
		usb_spi_enable(&ccd_usb_spi, 1);
		usb_init();

		/* Attempt to connect UART TX */
		uartn_tx_connect(UART_AP);
		uartn_tx_connect(UART_EC);

		/* Turn on 3.3V rail used for INAs and initialize I2CM module */
		usb_i2c_board_enable();
	} else {
		CPRINTS("CCD EXT disable");
		state = DEVICE_STATE_DISCONNECTED;

		usb_release();
		usb_console_enable(0, 0);
		usb_spi_enable(&ccd_usb_spi, 0);

		/* Disconnect from AP and EC UART TX peripheral from gpios */
		uartn_tx_disconnect(UART_EC);
		uartn_tx_disconnect(UART_AP);

		/* Turn off 3.3V rail to INAs and disconnect I2CM module */
		usb_i2c_board_disable();
	}
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
		} else
			return EC_ERROR_PARAM1;
	}

	print_ap_state();
	print_ec_state();
	print_rdd_state();
	print_servo_state();

	ccprintf("CCD EXT: %s\n",
		 ccd_ext_is_enabled() ? "enabled" : "disabled");
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
			"[uart|i2c] [<BOOLEAN>]",
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
