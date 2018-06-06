/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stddef.h"
#include "common.h"
#include "config.h"
#include "link_defs.h"
#include "registers.h"
#include "util.h"
#include "usb_api.h"
#include "usb_hw.h"
#include "usb_isochronous.h"


/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

/*
 * Currently, we only support TX direction for USB isochronous transfer.
 *
 * According to RM0091, isochronous transfer is always double buffered.
 * Addresses of buffers are pointed by `btable_ep[<endpoint>].tx_addr` and
 * `btable_ep[<endpoint>].rx_addr`.
 *
 * DTOG | USB Buffer | App Buffer
 * -----+------------+-----------
 *   0  | tx_addr    | rx_addr
 *   1  | rx_addr    | tx_addr
 *
 * That is, when DTOG bit is 0 (see `get_tx_dtog()`), USB hardware will read
 * from `tx_addr`, and our application can write new data to `rx_addr` at the
 * same time.
 *
 * Number of bytes in each buffer shall be tracked by `tx_count` and `rx_count`
 * respectively.
 *
 * `get_app_addr()`, `set_app_count()` help you to to select the correct
 * variable to use by given DTOG value, which is available by `get_tx_dtog()`.
 */

/*
 * Gets current DTOG value of given `config`.
 */
static int get_tx_dtog(struct usb_isochronous_config const *config)
{
	return !!(STM32_USB_EP(config->endpoint) & EP_TX_DTOG);
}

/*
 * Gets buffer address that can be used by software (application).
 *
 * The mapping between application buffer address and current TX DTOG value is
 * shown in table above.
 */
static usb_uint *get_app_addr(struct usb_isochronous_config const *config,
			      int dtog_value)
{
	return config->tx_ram[dtog_value];
}

/*
 * Sets number of bytes written to application buffer.
 */
static void set_app_count(struct usb_isochronous_config const *config,
			  int dtog_value,
			  usb_uint count)
{
	if (dtog_value)
		btable_ep[config->endpoint].tx_count = count;
	else
		btable_ep[config->endpoint].rx_count = count;
}

int usb_isochronous_write_buffer(
		struct usb_isochronous_config const *config,
		const uint8_t *src,
		size_t n,
		size_t dst_offset,
		int *buffer_id,
		int commit)
{
	int dtog_value = get_tx_dtog(config);
	usb_uint *buffer = get_app_addr(config, dtog_value);
	uintptr_t ptr = usb_sram_addr(buffer);

	if (*buffer_id == -1)
		*buffer_id = dtog_value;
	else if (dtog_value != *buffer_id)
		return -EC_ERROR_TIMEOUT;

	if (dst_offset > config->tx_size)
		return -EC_ERROR_INVAL;

	n = MIN(n, config->tx_size - dst_offset);
	memcpy_to_usbram((void *)(ptr + dst_offset), src, n);

	if (commit)
		set_app_count(config, dtog_value, dst_offset + n);

	return n;
}

void usb_isochronous_init(struct usb_isochronous_config const *config)
{
	int ep = config->endpoint;

	btable_ep[ep].tx_addr = usb_sram_addr(get_app_addr(config, 1));
	btable_ep[ep].rx_addr = usb_sram_addr(get_app_addr(config, 0));
	set_app_count(config, 0, 0);
	set_app_count(config, 1, 0);

	STM32_USB_EP(ep) = ((ep << 0) | /* Endpoint Addr */
			    EP_TX_VALID | /* start transmit */
			    (2 << 9) | /* ISO EP */
			    EP_RX_DISAB);
}

void usb_isochronous_event(struct usb_isochronous_config const *config,
			   enum usb_ep_event evt)
{
	if (evt == USB_EVENT_RESET)
		usb_isochronous_init(config);
}

void usb_isochronous_tx(struct usb_isochronous_config const *config)
{
	/*
	 * Clear CTR_TX, note that EP_TX_VALID will *NOT* be cleared by
	 * hardware, so we don't need to toggle it.
	 */
	STM32_TOGGLE_EP(config->endpoint, 0, 0, 0);
	/*
	 * Clear buffer count for buffer we just transmitted, so we do not
	 * transmit the data twice.
	 */
	set_app_count(config, get_tx_dtog(config), 0);

	config->tx_callback(config);
}

int usb_isochronous_iface_handler(struct usb_isochronous_config const *config,
				  usb_uint *ep0_buf_rx,
				  usb_uint *ep0_buf_tx)
{
	int ret = -1;

	if (ep0_buf_rx[0] == (USB_DIR_OUT |
			      USB_TYPE_STANDARD |
			      USB_RECIP_INTERFACE |
			      USB_REQ_SET_INTERFACE << 8)) {
		ret = config->set_interface(ep0_buf_rx[1], ep0_buf_rx[2]);

		if (ret == 0) {
			/* ACK */
			btable_ep[0].tx_count = 0;
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
		}
	}
	return ret;
}
