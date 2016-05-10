/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "rdd.h"
#include "registers.h"
#include "uartn.h"
#include "usb_api.h"

/* If the UART TX is enabled the pinmux select will have a non-zero value */
int uartn_enabled(int uart)
{
	if (uart == UART_AP)
		return GREAD(PINMUX, DIOA7_SEL);
	return GREAD(PINMUX, DIOB5_SEL);
}

static void usart_tx_connect(void)
{
	GWRITE(PINMUX, DIOA7_SEL, GC_PINMUX_UART1_TX_SEL);
	GWRITE(PINMUX, DIOB5_SEL, GC_PINMUX_UART2_TX_SEL);
}

static void usart_tx_disconnect(void)
{
	GWRITE(PINMUX, DIOA7_SEL, GC_PINMUX_DIOA7_SEL_DEFAULT);
	GWRITE(PINMUX, DIOB5_SEL, GC_PINMUX_DIOB5_SEL_DEFAULT);
}

void rdd_attached(void)
{
	/* Indicate case-closed debug mode (active low) */
	gpio_set_level(GPIO_CCD_MODE_L, 0);

	/* Select the CCD PHY */
	usb_select_phy(USB_SEL_PHY1);
}

void rdd_detached(void)
{
	/* Disconnect from AP and EC UART TX */
	usart_tx_disconnect();

	/* Done with case-closed debug mode */
	gpio_set_level(GPIO_CCD_MODE_L, 1);

	/* Select the AP PHY */
	usb_select_phy(USB_SEL_PHY0);
}

static int command_uart(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp("enable", argv[1])) {
			usart_tx_connect();
		} else if (!strcasecmp("disable", argv[1])) {
			usart_tx_disconnect();
		}
	}

	ccprintf("EC UART %s\nAP UART %s\n",
		uartn_enabled(UART_EC) ? "enabled" : "disabled",
		uartn_enabled(UART_AP) ? "enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(uart, command_uart,
	"[enable|disable]",
	"Get/set the UART TX connection state",
	NULL);
