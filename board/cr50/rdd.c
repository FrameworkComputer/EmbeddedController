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
#include "uartn.h"
#include "usb_api.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static int uart_enabled;

struct uart_config {
	const char *name;
	enum device_type device;
	int tx_signal;
};

static struct uart_config uarts[] = {
	[UART_AP] = {"AP", DEVICE_AP, GC_PINMUX_UART1_TX_SEL},
	[UART_EC] = {"EC", DEVICE_EC, GC_PINMUX_UART2_TX_SEL},
};

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
	if (!uart_enabled)
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

	ccd_set_mode(CCD_MODE_ENABLED);
}

void rdd_detached(void)
{
	/* Disconnect from AP and EC UART TX */
	uartn_tx_disconnect(UART_EC);
	uartn_tx_disconnect(UART_AP);

	/* Done with case-closed debug mode */
	gpio_set_level(GPIO_CCD_MODE_L, 1);

	ccd_set_mode(CCD_MODE_DISABLED);
}

static int command_ccd(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp("uart", argv[1]) && argc > 2) {
			if (!strcasecmp("enable", argv[2])) {
				uart_enabled = 1;
				uartn_tx_connect(UART_EC);
				uartn_tx_connect(UART_AP);
			} else if (!strcasecmp("disable", argv[2])) {
				uart_enabled = 0;
				uartn_tx_disconnect(UART_EC);
				uartn_tx_disconnect(UART_AP);
			}
		} else if (argc == 2) {
			if (!strcasecmp("enable", argv[1]))
				rdd_attached();
			else if (!strcasecmp("disable", argv[1]))
				rdd_detached();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("CCD:     %s\n", !gpio_get_level(GPIO_CCD_MODE_L) ?
		" enabled" : "disabled");
	ccprintf("AP UART: %s\nEC UART: %s\n",
		uartn_enabled(UART_AP) ? " enabled" : "disabled",
		uartn_enabled(UART_EC) ? " enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccd, command_ccd,
	"[uart] [enable|disable]",
	"Get/set the case closed debug state",
	NULL);
