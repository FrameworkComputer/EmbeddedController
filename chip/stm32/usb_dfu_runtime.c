/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dfu_bootmanager_shared.h"
#include "registers.h"
#include "usb_descriptor.h"
#include "usb_dfu_runtime.h"
#include "usb_hw.h"

/* DFU Run-Time Descriptor Set. */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_DFU) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_DFU,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_APP_SPEC,
	.bInterfaceSubClass = USB_DFU_RUNTIME_SUBCLASS,
	.bInterfaceProtocol = USB_DFU_RUNTIME_PROTOCOL,
	.iInterface = USB_STR_DFU_NAME,
};

/* DFU Functional Descriptor. */
const struct usb_runtime_dfu_functional_desc USB_CUSTOM_DESC_VAR(USB_IFACE_DFU,
						dfu, dfu_func_desc) = {
	.bLength = USB_DFU_RUNTIME_DESC_SIZE,
	.bDescriptorType = USB_DFU_RUNTIME_DESC_FUNCTIONAL,
	.bmAttributes = USB_DFU_RUNTIME_DESC_ATTRS,
	.wDetachTimeOut = USB_DFU_RUNTIME_DESC_DETACH_TIMEOUT,
	.wTransferSize = USB_DFU_RUNTIME_DESC_TRANSFER_SIZE,
	.bcdDFUVersion = USB_DFU_RUNTIME_DESC_DFU_VERSION,
};

static int dfu_runtime_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx)
{
	struct usb_setup_packet packet;

	usb_read_setup_packet(ep0_buf_rx, &packet);
	btable_ep[0].tx_count = 0;
	if ((packet.bmRequestType ==
		(USB_DIR_OUT | USB_TYPE_STANDARD |  USB_RECIP_INTERFACE)) &&
		(packet.bRequest == USB_REQ_SET_INTERFACE)) {
		/* ACK the change alternative mode request. */

		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
		return 0;
	} else if ((packet.bmRequestType ==
			(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) &&
			(packet.bRequest == USB_DFU_RUNTIME_REQ_DETACH)) {
		/* Host is requesting a jump from application to DFU mode. */

		STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
		return dfu_bootmanager_enter_dfu();
	} else if (packet.bmRequestType ==
			(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
		if (packet.bRequest == USB_DFU_RUNTIME_REQ_GET_STATUS) {
			/* Return the Get Status response. */

			struct usb_runtime_dfu_get_status_resp response = {
				.bStatus = USB_DFU_RUNTIME_STATUS_OK,
				.bState = USB_DFU_RUNTIME_STATE_APP_IDLE,
			};

			memcpy_to_usbram((void *) usb_sram_addr(ep0_buf_tx),
				&response, sizeof(response));
			btable_ep[0].tx_count = sizeof(response);
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
			return 0;
		}
		if (packet.bRequest == USB_DFU_RUNTIME_REQ_GET_STATE) {
			/* Return the Get State response. */

			struct usb_runtime_dfu_get_state_resp response = {
				.bState = USB_DFU_RUNTIME_STATE_APP_IDLE,
			};

			memcpy_to_usbram((void *) usb_sram_addr(ep0_buf_tx),
				&response, sizeof(response));
			btable_ep[0].tx_count = sizeof(response);
			STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_TX_RX_VALID, 0);
			return 0;
		}
	}
	/* Return a stall response for any unhandled packets. */

	STM32_TOGGLE_EP(0, EP_TX_RX_MASK, EP_RX_VALID | EP_TX_STALL, 0);
	return 0;
}

USB_DECLARE_IFACE(USB_IFACE_DFU, dfu_runtime_request)
