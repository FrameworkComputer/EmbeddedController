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
#include "usb.h"
#include "usb_ms.h"
#include "usb_ms_scsi.h"

/*
 * Implements the USB Mass Storage Class specification using the
 * Bulk-Only Transport (BBB) protocol with the transparent SCSI command set.
 */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBMS, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBMS, format, ## args)

/* Mass storage descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_MS) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_MS,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass = USB_MS_SUBCLASS_SCSI,
	.bInterfaceProtocol = USB_MS_PROTOCOL_BBB,
	.iInterface = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_MS, USB_EP_MS_TX) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | USB_EP_MS_TX,
	.bmAttributes = 0x02 /* Bulk */,
	.wMaxPacketSize = USB_MS_PACKET_SIZE,
	.bInterval = 0,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_MS, USB_EP_MS_RX) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_MS_RX,
	.bmAttributes = 0x02 /* Bulk */,
	.wMaxPacketSize = USB_MS_PACKET_SIZE,
	.bInterval = 0,
};

/* USB mass storage state machine */
static enum usb_ms_state {
	USB_MS_STATE_IDLE,
	USB_MS_STATE_BUSY,
	USB_MS_STATE_ERROR, /* received an invalid CBW */
	USB_MS_STATE_PHASE_ERROR,
} ms_state = USB_MS_STATE_IDLE;

/* Hardware buffers for USB endpoints */
usb_uint ms_ep_tx[USB_MS_PACKET_SIZE] __usb_ram;
usb_uint ms_ep_rx[USB_MS_PACKET_SIZE] __usb_ram;

static void ms_tx_reset(void)
{
	btable_ep[USB_EP_MS_TX].tx_addr = usb_sram_addr(ms_ep_tx);
	btable_ep[USB_EP_MS_TX].tx_count = 0;
	btable_ep[USB_EP_MS_TX].rx_count = 0;

	STM32_USB_EP(USB_EP_MS_TX) =
				(USB_EP_MS_TX << 0) /* Endpoint Address */ |
				(2 << 4) /* TX NAK */ |
				(0 << 9) /* Bulk EP */ |
				(0 << 12) /* RX Disabled */;

	ms_state = USB_MS_STATE_IDLE;
	scsi_reset();
}

static void ms_rx_reset(void)
{
	btable_ep[USB_EP_MS_RX].rx_addr = usb_sram_addr(ms_ep_rx);
	btable_ep[USB_EP_MS_RX].rx_count = 0x8000 |
				((USB_MS_PACKET_SIZE/32-1) << 10);
	btable_ep[USB_EP_MS_RX].tx_count = 0;

	STM32_USB_EP(USB_EP_MS_RX) =
				(USB_EP_MS_RX << 0) /* Endpoint Address */ |
				(0 << 4) /* TX Disabled */ |
				(0 << 9) /* Bulk EP */ |
				(3 << 12) /* RX VALID */;

	ms_state = USB_MS_STATE_IDLE;
	scsi_reset();
}

/*
 * Construct and send a CSW.
 */
static void ms_send_csw(int ms_tag, int ms_xfer_len,
	int scsi_rv, int scsi_xfer_len)
{
	struct usb_ms_csw *resp = (struct usb_ms_csw *) ms_ep_tx;

	/* construct CSW response */
	resp->signature = UBS_MS_CSW_SIGNATURE;
	resp->tag = ms_tag;
	resp->data_residue = (ms_xfer_len > scsi_xfer_len) ?
				(ms_xfer_len - scsi_xfer_len) :
				(scsi_xfer_len - ms_xfer_len);
	if (scsi_rv != SCSI_SENSE_HARDWARE_ERROR)
		resp->status = (scsi_rv == SCSI_SENSE_NO_SENSE) ?
				USB_MS_CSW_CMD_PASSED :
				USB_MS_CSW_CMD_FAILED;
	else {
		ms_state = USB_MS_STATE_PHASE_ERROR;
		resp->status = USB_MS_CSW_CMD_PHASE_ERR;
	}

	/* set CSW response length */
	btable_ep[USB_EP_MS_TX].tx_count = USB_MS_CSW_LENGTH;

	/* wait for data to be read */
	STM32_TOGGLE_EP(USB_EP_MS_TX, EP_TX_MASK, EP_TX_VALID, 0);
}

/*
 * Send data already in the output buffer.
 */
static void ms_send_data(int ms_xfer_len, int *scsi_xfer_len)
{
	/* truncate if necessary */
	if (btable_ep[USB_EP_MS_TX].tx_count > ms_xfer_len)
		btable_ep[USB_EP_MS_TX].tx_count = ms_xfer_len;

	/* increment sent data counter with actual length */
	*scsi_xfer_len += btable_ep[USB_EP_MS_TX].tx_count;

	/* wait for data to be read */
	STM32_TOGGLE_EP(USB_EP_MS_TX, EP_TX_MASK, EP_TX_VALID, 0);
}

static void ms_tx(void)
{
	task_set_event(TASK_ID_USB_MS, TASK_EVENT_CUSTOM(USB_MS_EVENT_TX), 0);

	STM32_USB_EP(USB_EP_MS_TX) &= EP_MASK;
}

static void ms_rx(void)
{
	task_set_event(TASK_ID_USB_MS, TASK_EVENT_CUSTOM(USB_MS_EVENT_RX), 0);

	STM32_USB_EP(USB_EP_MS_RX) &= EP_MASK;
}
USB_DECLARE_EP(USB_EP_MS_TX, ms_tx, ms_tx, ms_tx_reset);
USB_DECLARE_EP(USB_EP_MS_RX, ms_rx, ms_rx, ms_rx_reset);

static int ms_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx)
{
	uint16_t *req = (uint16_t *) ep0_buf_rx;

	if ((req[0] & (USB_DIR_OUT | USB_RECIP_INTERFACE | USB_TYPE_CLASS)) !=
	    (USB_DIR_OUT | USB_RECIP_INTERFACE | USB_TYPE_CLASS))
		return 1;

	switch (req[0] >> 8) {
	case USB_MS_REQ_RESET:
		if (req[1] == 0 && req[2] == USB_IFACE_MS &&
		    req[3] == 0) {
			ms_rx_reset();
		}
		break;
	case USB_MS_REQ_GET_MAX_LUN:
		if (req[1] == 0 && req[2] == USB_IFACE_MS &&
		    req[3] == 1) {
			ep0_buf_tx[0] = SCSI_MAX_LUN;
			btable_ep[0].tx_count = sizeof(uint8_t);
			STM32_TOGGLE_EP(USB_EP_CONTROL, EP_TX_RX_MASK,
					EP_TX_RX_VALID, 0);
		}
		break;
	}

	return 0;
}
USB_DECLARE_IFACE(USB_IFACE_MS, ms_iface_request);

void ms_task(void)
{
	struct usb_ms_cbw *req = (struct usb_ms_cbw *) ms_ep_rx;
	int scsi_rv, scsi_xfer_len = 0;
	uint32_t ms_xfer_len = 0, ms_tag = 0;
	uint8_t ms_dir = 0, evt;

	while (1) {
		/* wait for event or usb reset */
		evt = (task_wait_event(-1) & 0xff);

		switch (ms_state) {
		case USB_MS_STATE_IDLE:
			/* receiving data */
			if (evt & USB_MS_EVENT_RX) {
				/* CBW is not valid or meaningful */
				if ((btable_ep[USB_EP_MS_RX].rx_count & 0x3ff)
				     != USB_MS_CBW_LENGTH ||
				     req->signature
				     != USB_MS_CBW_SIGNATURE ||
				     req->LUN & 0xf0 ||
				     req->length & 0xe0 ||
				     req->LUN > SCSI_MAX_LUN) {

					ms_state = USB_MS_STATE_ERROR;
					STM32_TOGGLE_EP(USB_EP_MS_TX,
						EP_TX_MASK, EP_TX_STALL, 0);
					STM32_TOGGLE_EP(USB_EP_MS_RX,
						EP_RX_MASK, EP_RX_STALL, 0);
					break;
				}

				/* have new packet */
				ms_state = USB_MS_STATE_BUSY;

				/* record packet details */
				ms_tag = req->tag;
				ms_xfer_len = req->data_transfer_length;
				ms_dir = req->flags;
				scsi_xfer_len = 0;

				/* parse message and get next state */
				scsi_rv = scsi_parse(req->command_block,
					req->length);
				if (scsi_rv == SCSI_STATUS_CONTINUE) {
					if (ms_dir & USB_MS_CBW_DATA_IN)
						/* send out data */
						ms_send_data(ms_xfer_len,
							&scsi_xfer_len);
					else
						/* receive more data */
						STM32_TOGGLE_EP(USB_EP_MS_RX,
						 EP_RX_MASK, EP_RX_VALID, 0);
				} else {
					/* send message response */
					ms_state = USB_MS_STATE_IDLE;
					ms_send_csw(ms_tag, ms_xfer_len,
						scsi_rv, scsi_xfer_len);
				}
			} else if (evt & USB_MS_EVENT_TX) {
				/* just sent CSW, wait for next CBW */
				STM32_TOGGLE_EP(USB_EP_MS_RX, EP_RX_MASK,
					EP_RX_VALID, 0);
			}
		break;
		case USB_MS_STATE_BUSY:
			/* receiving data */
			if (evt & USB_MS_EVENT_RX) {
				/*
				 * received at least two CBW's in a row,
				 * go to error state
				 */
				if (ms_dir & USB_MS_CBW_DATA_IN) {
					ms_state = USB_MS_STATE_ERROR;
					STM32_TOGGLE_EP(USB_EP_MS_TX,
						EP_TX_MASK, EP_TX_STALL, 0);
					STM32_TOGGLE_EP(USB_EP_MS_RX,
						EP_RX_MASK, EP_RX_STALL, 0);
					break;
				}
				/* receive data */
				scsi_xfer_len +=
				 (btable_ep[USB_EP_MS_RX].rx_count & 0x3ff);
				scsi_rv = scsi_parse(NULL, 0);
				if (scsi_rv != SCSI_STATUS_CONTINUE) {
					ms_state = USB_MS_STATE_IDLE;
					ms_send_csw(ms_tag, ms_xfer_len,
						scsi_rv, scsi_xfer_len);
				}

				/* wait for more data */
				STM32_TOGGLE_EP(USB_EP_MS_RX,
					EP_RX_MASK, EP_RX_VALID, 0);
			} else if (evt & USB_MS_EVENT_TX) {
				/* reparse message and get next state */
				scsi_rv = scsi_parse(req->command_block,
					req->length);
				if (scsi_rv == SCSI_STATUS_CONTINUE) {
					ms_send_data(ms_xfer_len,
						&scsi_xfer_len);
				} else {
					ms_state = USB_MS_STATE_IDLE;
					ms_send_csw(ms_tag, ms_xfer_len,
						scsi_rv, scsi_xfer_len);
				}
			}
		break;
		case USB_MS_STATE_ERROR:
			/* maintain error state until reset recovery */
		break;
		case USB_MS_STATE_PHASE_ERROR:
			CPUTS("phase error!\n");

			STM32_TOGGLE_EP(USB_EP_MS_TX, EP_TX_MASK,
				EP_TX_STALL, 0);
			STM32_TOGGLE_EP(USB_EP_MS_RX, EP_RX_MASK,
				EP_RX_STALL, 0);
		break;
		default:
		break;
		}
	}
}
