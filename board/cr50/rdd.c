/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "device_state.h"
#include "gpio.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "uartn.h"
#include "usb_api.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static int uart_enabled, enable_usb_wakeup;

struct uart_config {
	const char *name;
	enum device_type device;
	int tx_signal;
};

static struct uart_config uarts[] = {
	[UART_AP] = {"AP", DEVICE_AP, GC_PINMUX_UART1_TX_SEL},
	[UART_EC] = {"EC", DEVICE_EC, GC_PINMUX_UART2_TX_SEL},
};

static int ccd_is_enabled(void)
{
	return !gpio_get_level(GPIO_CCD_MODE_L);
}

int is_utmi_wakeup_allowed(void)
{
	return enable_usb_wakeup;
}


/* If the UART TX is enabled the pinmux select will have a non-zero value */
int uartn_enabled(int uart)
{
	if (uart == UART_AP)
		return GREAD(PINMUX, DIOA7_SEL);
	return GREAD(PINMUX, DIOB5_SEL);
}

/* Connect the UART pin to the given signal */
static void uart_select_tx(int uart, int signal)
{
	if (uart == UART_AP)
		GWRITE(PINMUX, DIOA7_SEL, signal);
	else
		GWRITE(PINMUX, DIOB5_SEL, signal);
}

static int servo_is_connected(void)
{
	return device_get_state(DEVICE_SERVO) == DEVICE_STATE_ON;
}

void uartn_tx_connect(int uart)
{
	if (!uart_enabled || !ccd_is_enabled())
		return;

	if (servo_is_connected()) {
		CPRINTS("Servo is attached cannot enable %s UART",
			uarts[uart].name);
		return;
	}

	if (device_get_state(uarts[uart].device) == DEVICE_STATE_ON)
		uart_select_tx(uart, uarts[uart].tx_signal);
	else if (!uartn_enabled(uart))
		CPRINTS("%s is powered off", uarts[uart].name);
}

void uartn_tx_disconnect(int uart)
{
	/* If servo is connected disable UART */
	if (servo_is_connected())
		uart_enabled = 0;

	/* Disconnect the TX pin from UART peripheral */
	uart_select_tx(uart, 0);
}

void rdd_attached(void)
{
	/* Indicate case-closed debug mode (active low) */
	gpio_set_level(GPIO_CCD_MODE_L, 0);

	/* Enable CCD */
	ccd_set_mode(CCD_MODE_ENABLED);

	enable_usb_wakeup = 1;

	/* Enable device state monitoring */
	device_detect_state_enable(1);
}

void rdd_detached(void)
{
	/* Disable device state monitoring */
	device_detect_state_enable(0);

	/* Disconnect from AP and EC UART TX peripheral from gpios */
	uartn_tx_disconnect(UART_EC);
	uartn_tx_disconnect(UART_AP);

	/* Disable the AP and EC UART peripheral */
	uartn_disable(UART_AP);
	uartn_disable(UART_EC);

	/* Done with case-closed debug mode */
	gpio_set_level(GPIO_CCD_MODE_L, 1);

	enable_usb_wakeup = 0;

	/* Disable CCD */
	ccd_set_mode(CCD_MODE_DISABLED);
}

void ccd_phy_init(int enable_ccd)
{
	uint32_t properties = system_get_board_properties();
	/*
	 * For boards that have one phy connected to the AP and one to the
	 * external port PHY0 is for the AP and PHY1 is for CCD.
	 */
	uint32_t which_phy = enable_ccd ? USB_SEL_PHY1 : USB_SEL_PHY0;

	/*
	 * TODO: if both PHYs are connected to the external port select the
	 * PHY based on the detected polarity
	 */
	usb_select_phy(which_phy);

	/*
	 * If the board has the non-ccd phy connected to the AP initialize the
	 * phy no matter what. Otherwise only initialized the phy if ccd is
	 * enabled.
	 */
	if ((properties & BOARD_USB_AP) || enable_ccd)
		usb_init();
}

static int command_ccd(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!strcasecmp("uart", argv[1]) && argc > 2) {
			if (!parse_bool(argv[2], &val))
				return EC_ERROR_PARAM2;

			if (val) {
				uart_enabled = 1;
				uartn_tx_connect(UART_EC);
				uartn_tx_connect(UART_AP);
			} else {
				uart_enabled = 0;
				uartn_tx_disconnect(UART_EC);
				uartn_tx_disconnect(UART_AP);
			}
		} else if (argc == 2) {
			if (!parse_bool(argv[1], &val))
				return EC_ERROR_PARAM1;

			if (val)
				rdd_attached();
			else
				rdd_detached();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("CCD:     %s\nAP UART: %s\nEC UART: %s\n",
		ccd_is_enabled() ? " enabled" : "disabled",
		uartn_enabled(UART_AP) ? " enabled" : "disabled",
		uartn_enabled(UART_EC) ? " enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccd, command_ccd,
			"[uart] [<BOOLEAN>]",
			"Get/set the case closed debug state");
