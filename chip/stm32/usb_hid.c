/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "link_defs.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_descriptor.h"
#include "usb_hid.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

void hid_tx(int ep)
{
	/* clear IT */
	STM32_USB_EP(ep) = (STM32_USB_EP(ep) & EP_MASK);
}

void hid_reset(int ep, usb_uint *hid_ep_buf, int len)
{
	int i;
	/* HID interrupt endpoint 1 */
	btable_ep[ep].tx_addr = usb_sram_addr(hid_ep_buf);
	btable_ep[ep].tx_count = len;
	for (i = 0; i < (len+1)/2; i++)
		hid_ep_buf[i] = 0;
	STM32_USB_EP(ep) = (ep << 0) /* Endpoint Address */ |
		(3 << 4) /* TX Valid */ |
		(3 << 9) /* interrupt EP */ |
		(0 << 12) /* RX Disabled */;
}

int hid_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx,
		      const uint8_t *report_desc, int report_size)
{
	if ((ep0_buf_rx[0] == (USB_DIR_IN | USB_RECIP_INTERFACE |
			      (USB_REQ_GET_DESCRIPTOR << 8))) &&
			      (ep0_buf_rx[1] == (USB_HID_DT_REPORT << 8))) {
		/* Setup : HID specific : Get Report descriptor */
		memcpy_to_usbram((void *) usb_sram_addr(ep0_buf_tx),
				 report_desc,
				 report_size);
		btable_ep[0].tx_count = MIN(ep0_buf_rx[3], report_size);
		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
			  EP_STATUS_OUT);
		CPRINTF("RPT %04x[l %04x]\n", STM32_USB_EP(0),
			ep0_buf_rx[3]);
		return 0;
	}

	return 1;
}
