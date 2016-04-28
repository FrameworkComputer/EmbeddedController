/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "rdd.h"
#include "registers.h"
#include "usb_api.h"

static void usart_tx_connect(void)
{
	GWRITE(PINMUX, DIOA7_SEL, GC_PINMUX_UART1_TX_SEL);
	GWRITE(PINMUX, DIOB5_SEL, GC_PINMUX_UART2_TX_SEL);
}

static void usart_tx_disconnect(void)
{
	GWRITE(PINMUX, DIOA7_SEL, GC_PINMUX_DIOA3_SEL_DEFAULT);
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
	static int enabled;

	if (argc > 1) {
		if (!strcasecmp("enable", argv[1])) {
			enabled = 1;
			usart_tx_connect();
		} else if (!strcasecmp("disable", argv[1])) {
			enabled = 0;
			usart_tx_disconnect();
		}
	}

	ccprintf("UART %s\n", enabled ? "enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(uart, command_uart,
	"[enable|disable]",
	"Get/set the UART TX connection state",
	NULL);
