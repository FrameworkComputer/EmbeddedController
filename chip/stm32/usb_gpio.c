/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "link_defs.h"
#include "registers.h"
#include "usb_gpio.h"

void usb_gpio_tx(struct usb_gpio_config const *config)
{
	size_t   i;
	uint32_t mask  = 1;
	uint32_t value = 0;

	for (i = 0; i < config->num_gpios; ++i, mask <<= 1)
		value |= (gpio_get_level(config->gpios[i])) ? mask : 0;

	config->tx_ram[0] = value;
	config->tx_ram[1] = value >> 16;

	btable_ep[config->endpoint].tx_count = USB_GPIO_TX_PACKET_SIZE;

	/*
	 * TX packet updated, mark the packet as VALID.
	 */
	STM32_TOGGLE_EP(config->endpoint, EP_TX_MASK, EP_TX_VALID, 0);
}

void usb_gpio_rx(struct usb_gpio_config const *config)
{
	size_t   i;
	uint32_t mask        = 1;
	uint32_t set_mask    = ((uint32_t)(config->rx_ram[0]) |
			        (uint32_t)(config->rx_ram[1]) << 16);
	uint32_t clear_mask  = ((uint32_t)(config->rx_ram[2]) |
			        (uint32_t)(config->rx_ram[3]) << 16);
	uint32_t ignore_mask = set_mask & clear_mask;

	if ((btable_ep[config->endpoint].rx_count & 0x3ff) ==
	    USB_GPIO_RX_PACKET_SIZE) {
		for (i = 0; i < config->num_gpios; ++i, mask <<= 1) {
			if (ignore_mask & mask)
				;
			else if (set_mask & mask)
				gpio_set_level(config->gpios[i], 1);
			else if (clear_mask & mask)
				gpio_set_level(config->gpios[i], 0);
		}
	}

	/*
	 * RX packet consumed, mark the packet as VALID.
	 */
	STM32_TOGGLE_EP(config->endpoint, EP_RX_MASK, EP_RX_VALID, 0);
}

void usb_gpio_reset(struct usb_gpio_config const *config)
{
	int i = config->endpoint;

	btable_ep[i].tx_addr  = usb_sram_addr(config->tx_ram);
	btable_ep[i].tx_count = USB_GPIO_TX_PACKET_SIZE;

	btable_ep[i].rx_addr  = usb_sram_addr(config->rx_ram);
	btable_ep[i].rx_count = ((USB_GPIO_RX_PACKET_SIZE / 2) << 10);

	/*
	 * Initialize TX buffer with zero, the first IN transaction will fill
	 * this in with a valid value.
	 */
	config->tx_ram[0] = 0;
	config->tx_ram[1] = 0;

	STM32_USB_EP(i) = ((i <<  0) | /* Endpoint Addr*/
			   (3 <<  4) | /* TX Valid */
			   (0 <<  9) | /* Bulk EP */
			   (3 << 12)); /* RX Valid */
}
