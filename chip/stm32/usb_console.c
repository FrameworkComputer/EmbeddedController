/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "link_defs.h"
#include "printf.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

#define USB_CONSOLE_TIMEOUT_US (30 * MSEC)
#define USB_CONSOLE_RX_BUF_SIZE 64
#define RX_BUF_NEXT(i) (((i) + 1) & (USB_CONSOLE_RX_BUF_SIZE - 1))

static volatile char rx_buf[USB_CONSOLE_RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;

static int last_tx_ok = 1;

static int is_reset;
static int is_enabled = 1;

/* USB-Serial descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_CONSOLE) = {
	.bLength            = USB_DT_INTERFACE_SIZE,
	.bDescriptorType    = USB_DT_INTERFACE,
	.bInterfaceNumber   = USB_IFACE_CONSOLE,
	.bAlternateSetting  = 0,
	.bNumEndpoints      = 2,
	.bInterfaceClass    = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_SERIAL,
	.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_SERIAL,
	.iInterface         = USB_STR_CONSOLE_NAME,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 0) = {
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = 0x80 | USB_EP_CONSOLE,
	.bmAttributes       = 0x02 /* Bulk IN */,
	.wMaxPacketSize     = USB_MAX_PACKET_SIZE,
	.bInterval          = 10
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 1) = {
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = USB_EP_CONSOLE,
	.bmAttributes       = 0x02 /* Bulk OUT */,
	.wMaxPacketSize     = USB_MAX_PACKET_SIZE,
	.bInterval          = 0
};

static usb_uint ep_buf_tx[USB_MAX_PACKET_SIZE / 2] __usb_ram;
static usb_uint ep_buf_rx[USB_MAX_PACKET_SIZE / 2] __usb_ram;

static void con_ep_tx(void)
{
	/* clear IT */
	STM32_TOGGLE_EP(USB_EP_CONSOLE, 0, 0, 0);
}

static void con_ep_rx(void)
{
	int i;
	for (i = 0; i < (btable_ep[USB_EP_CONSOLE].rx_count & 0x3ff); i++) {
		int rx_buf_next = RX_BUF_NEXT(rx_buf_head);
		if (rx_buf_next != rx_buf_tail) {
			rx_buf[rx_buf_head] = ((i & 1) ?
					       (ep_buf_rx[i >> 1] >> 8) :
					       (ep_buf_rx[i >> 1] & 0xff));
			rx_buf_head = rx_buf_next;
		}
	}

	/* clear IT */
	STM32_TOGGLE_EP(USB_EP_CONSOLE, EP_RX_MASK, EP_RX_VALID, 0);

	/* wake-up the console task */
	console_has_input();
}

static void ep_reset(void)
{
	btable_ep[USB_EP_CONSOLE].tx_addr  = usb_sram_addr(ep_buf_tx);
	btable_ep[USB_EP_CONSOLE].tx_count = 0;

	btable_ep[USB_EP_CONSOLE].rx_addr  = usb_sram_addr(ep_buf_rx);
	btable_ep[USB_EP_CONSOLE].rx_count =
		0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	STM32_USB_EP(USB_EP_CONSOLE) = (USB_EP_CONSOLE | /* Endpoint Addr */
					(2 << 4)       | /* TX NAK        */
					(0 << 9)       | /* Bulk EP       */
					(3 << 12));      /* RX VALID      */

	is_reset = 1;
}

USB_DECLARE_EP(USB_EP_CONSOLE, con_ep_tx, con_ep_rx, ep_reset);

static int __tx_char(void *context, int c)
{
	usb_uint *buf = (usb_uint *)ep_buf_tx;
	int *tx_idx = context;

	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(context, '\r'))
		return 1;

	if (*tx_idx > 63)
		return 1;
	if (!(*tx_idx & 1))
		buf[*tx_idx/2] = c;
	else
		buf[*tx_idx/2] |= c << 8;
	(*tx_idx)++;

	return 0;
}

static void usb_enable_tx(int len)
{
	if (!is_enabled)
		return;

	btable_ep[USB_EP_CONSOLE].tx_count = len;
	STM32_TOGGLE_EP(USB_EP_CONSOLE, EP_TX_MASK, EP_TX_VALID, 0);
}

static inline int usb_console_tx_valid(void)
{
	return (STM32_USB_EP(USB_EP_CONSOLE) & EP_TX_MASK) == EP_TX_VALID;
}

static int usb_wait_console(void)
{
	timestamp_t deadline = get_time();
	int wait_time_us = 1;

	if (!is_enabled || !usb_is_enabled())
		return EC_SUCCESS;

	deadline.val += USB_CONSOLE_TIMEOUT_US;

	/*
	 * If the USB console is not used, Tx buffer would never free up.
	 * In this case, let's drop characters immediately instead of sitting
	 * for some time just to time out. On the other hand, if the last
	 * Tx is good, it's likely the host is there to receive data, and
	 * we should wait so that we don't clobber the buffer.
	 */
	if (last_tx_ok) {
		while (usb_console_tx_valid() || !is_reset) {
			if (timestamp_expired(deadline, NULL) ||
			    in_interrupt_context()) {
				last_tx_ok = 0;
				return EC_ERROR_TIMEOUT;
			}
			if (wait_time_us < MSEC)
				udelay(wait_time_us);
			else
				usleep(wait_time_us);
			wait_time_us *= 2;
		}

		return EC_SUCCESS;
	} else {
		last_tx_ok = !usb_console_tx_valid();
		return EC_SUCCESS;
	}
}

/*
 * Public USB console implementation below.
 */
int usb_getc(void)
{
	int c;

	if (rx_buf_tail == rx_buf_head)
		return -1;

	if (!is_enabled)
		return -1;

	c = rx_buf[rx_buf_tail];
	rx_buf_tail = RX_BUF_NEXT(rx_buf_tail);
	return c;
}

int usb_putc(int c)
{
	int ret;
	int tx_idx = 0;

	ret = usb_wait_console();
	if (ret)
		return ret;

	ret = __tx_char(&tx_idx, c);
	usb_enable_tx(tx_idx);

	return ret;
}

int usb_puts(const char *outstr)
{
	int ret;
	int tx_idx = 0;

	ret = usb_wait_console();
	if (ret)
		return ret;

	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(&tx_idx, *outstr++) != 0)
			break;
	}

	usb_enable_tx(tx_idx);

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int usb_vprintf(const char *format, va_list args)
{
	int ret;
	int tx_idx = 0;

	ret = usb_wait_console();
	if (ret)
		return ret;

	ret = vfnprintf(__tx_char, &tx_idx, format, args);

	usb_enable_tx(tx_idx);
	return ret;
}

void usb_console_enable(int enabled)
{
	is_enabled = enabled;
}
