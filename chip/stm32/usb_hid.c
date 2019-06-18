/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
#include "usb_hw.h"
#include "usb_hid.h"
#include "usb_hid_hw.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

void hid_tx(int ep)
{
	/* clear IT */
	STM32_USB_EP(ep) = (STM32_USB_EP(ep) & EP_MASK);
}

void hid_reset(int ep, usb_uint *hid_ep_tx_buf, int tx_len,
	       usb_uint *hid_ep_rx_buf, int rx_len)
{
	int i;
	uint16_t ep_reg;

	btable_ep[ep].tx_addr = usb_sram_addr(hid_ep_tx_buf);
	btable_ep[ep].tx_count = tx_len;

	/* STM32 USB SRAM needs to be accessed one U16 at a time */
	for (i = 0; i < DIV_ROUND_UP(tx_len, 2); i++)
		hid_ep_tx_buf[i] = 0;

	ep_reg = (ep << 0) /* Endpoint Address */ |
		EP_TX_VALID |
		(3 << 9) /* interrupt EP */ |
		EP_RX_DISAB;

	/* Enable RX for output reports */
	if (hid_ep_rx_buf && rx_len > 0) {
		btable_ep[ep].rx_addr = usb_sram_addr(hid_ep_rx_buf);
		btable_ep[ep].rx_count = ((rx_len + 1) / 2) << 10;

		ep_reg |= EP_RX_VALID;  /* RX Valid */
	}

	STM32_USB_EP(ep) = ep_reg;
}

/*
 * Keep track of state in case we need to be called multiple times,
 * if the report length is bigger than 64 bytes.
 */
static int report_left;
static const uint8_t *report_ptr;

/*
 * Send report through ep0_buf_tx.
 *
 * If report size is greater than USB packet size (64 bytes), rest of the
 * reports will be saved in `report_ptr` and `report_left`, so we can call this
 * function again to send the remain parts.
 *
 * @return 0 if entire report is sent, 1 if there are remaining data.
 */
static int send_report(usb_uint *ep0_buf_tx,
		       const uint8_t *report,
		       int report_size)
{
	int packet_size = MIN(report_size, USB_MAX_PACKET_SIZE);

	memcpy_to_usbram((void *) usb_sram_addr(ep0_buf_tx),
			 report, packet_size);
	btable_ep[0].tx_count = packet_size;
	/* report_left != 0 if report doesn't fit in 1 packet. */
	report_left = report_size - packet_size;
	report_ptr = report + packet_size;

	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
			report_left ? 0 : EP_STATUS_OUT);

	return report_left ? 1 : 0;
}

int hid_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx,
		      const struct usb_hid_config_t *config)
{
	const uint8_t *report_desc = config->report_desc;
	int report_size = config->report_size;
	const struct usb_hid_descriptor *hid_desc = config->hid_desc;

	if (!ep0_buf_rx) {
		/*
		 * Continue previous transfer. We ignore report_desc/size here,
		 * which is fine as only one GET_DESCRIPTOR command comes at a
		 * time.
		 */
		if (report_left == 0)
			return -1;
		report_size = MIN(USB_MAX_PACKET_SIZE, report_left);
		memcpy_to_usbram((void *) usb_sram_addr(ep0_buf_tx),
				 report_ptr, report_size);
		btable_ep[0].tx_count = report_size;
		report_left -= report_size;
		report_ptr += report_size;
		STM32_TOGGLE_EP(0, EP_TX_MASK, EP_TX_VALID,
				report_left ? 0 : EP_STATUS_OUT);
		return report_left ? 1 : 0;
	} else if (ep0_buf_rx[0] == (USB_DIR_IN | USB_RECIP_INTERFACE |
					(USB_REQ_GET_DESCRIPTOR << 8))) {
		if (ep0_buf_rx[1] == (USB_HID_DT_REPORT << 8)) {
			/* Setup : HID specific : Get Report descriptor */
			return send_report(ep0_buf_tx, report_desc,
					   MIN(ep0_buf_rx[3], report_size));
		} else if (ep0_buf_rx[1] == (USB_HID_DT_HID << 8)) {
			/* Setup : HID specific : Get HID descriptor */
			memcpy_to_usbram_ep0_patch(hid_desc, sizeof(*hid_desc));
			btable_ep[0].tx_count = sizeof(*hid_desc);
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID,
					EP_STATUS_OUT);
			return 0;
		}
	} else if (ep0_buf_rx[0] == (USB_DIR_IN |
				     USB_RECIP_INTERFACE |
				     USB_TYPE_CLASS |
				     (USB_HID_REQ_GET_REPORT << 8))) {
		const uint8_t report_type = (ep0_buf_rx[1] >> 8) & 0xFF;
		const uint8_t report_id = ep0_buf_rx[1] & 0xFF;
		int retval;

		report_left = ep0_buf_rx[3];
		if (!config->get_report) /* not supported */
			return -1;

		retval = config->get_report(report_id,
					    report_type,
					    &report_ptr,
					    &report_left);
		if (retval)
			return retval;

		return send_report(ep0_buf_tx, report_ptr, report_left);
	}

	return -1;
}
