/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "link_defs.h"
#include "printf.h"
#include "queue.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "util.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)
#define USB_CONSOLE_TIMEOUT_US (30 * MSEC)

static struct queue const tx_q =
	QUEUE_NULL(CONFIG_USB_CONSOLE_TX_BUF_SIZE, uint8_t);
static struct queue const rx_q = QUEUE_NULL(USB_MAX_PACKET_SIZE, uint8_t);

static int last_tx_ok = 1;

static int is_reset;
static int is_enabled = 1;
static int is_readonly;

/* USB-Serial descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_CONSOLE) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_CONSOLE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = USB_SUBCLASS_GOOGLE_SERIAL,
	.bInterfaceProtocol = USB_PROTOCOL_GOOGLE_SERIAL,
	.iInterface = USB_STR_CONSOLE_NAME,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 0) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x80 | USB_EP_CONSOLE,
	.bmAttributes = 0x02 /* Bulk IN */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 10
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 1) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_CONSOLE,
	.bmAttributes = 0x02 /* Bulk OUT */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 0
};

static usb_uint ep_buf_tx[USB_MAX_PACKET_SIZE / 2] __usb_ram;
static usb_uint ep_buf_rx[USB_MAX_PACKET_SIZE / 2] __usb_ram;

/* Forward declaration */
static void handle_output(void);

static void con_ep_tx(void)
{
	/* clear IT */
	STM32_TOGGLE_EP(USB_EP_CONSOLE, 0, 0, 0);

	/* Check bytes in the FIFO needed to transmitted */
	handle_output();
}

static void con_ep_rx(void)
{
	int i;

	for (i = 0; i < (btable_ep[USB_EP_CONSOLE].rx_count & RX_COUNT_MASK);
	     i++) {
		int val = ((i & 1) ? (ep_buf_rx[i >> 1] >> 8) :
				     (ep_buf_rx[i >> 1] & 0xff));

		QUEUE_ADD_UNITS(&rx_q, &val, 1);
	}

	/* clear IT */
	STM32_TOGGLE_EP(USB_EP_CONSOLE, EP_RX_MASK, EP_RX_VALID, 0);

	/* wake-up the console task */
	console_has_input();
}

static void ep_event(enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;

	btable_ep[USB_EP_CONSOLE].tx_addr = usb_sram_addr(ep_buf_tx);
	btable_ep[USB_EP_CONSOLE].tx_count = 0;

	btable_ep[USB_EP_CONSOLE].rx_addr = usb_sram_addr(ep_buf_rx);
	btable_ep[USB_EP_CONSOLE].rx_count =
		0x8000 | ((USB_MAX_PACKET_SIZE / 32 - 1) << 10);

	STM32_USB_EP(USB_EP_CONSOLE) =
		(USB_EP_CONSOLE | /* Endpoint Addr */
		 (2 << 4) | /* TX NAK        */
		 (0 << 9) | /* Bulk EP       */
		 (is_readonly ? EP_RX_NAK : EP_RX_VALID));

	is_reset = 1;
}

USB_DECLARE_EP(USB_EP_CONSOLE, con_ep_tx, con_ep_rx, ep_event);

static int __tx_char(void *context, int c)
{
	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(context, '\r'))
		return 1;

	/* Return 0 on success */
	return !QUEUE_ADD_UNITS(&tx_q, &c, 1);
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
			if (timestamp_expired(deadline, NULL)) {
				last_tx_ok = 0;
				return EC_ERROR_TIMEOUT;
			}
			if (wait_time_us < MSEC)
				udelay(wait_time_us);
			else
				crec_usleep(wait_time_us);
			wait_time_us *= 2;
		}

		return EC_SUCCESS;
	} else {
		last_tx_ok = !usb_console_tx_valid();
		return EC_SUCCESS;
	}
}

/* Try to send some bytes from the Tx FIFO to the host */
static void tx_fifo_handler(void)
{
	int ret;
	size_t count;
	usb_uint *buf = (usb_uint *)ep_buf_tx;

	if (!is_reset)
		return;

	ret = usb_wait_console();
	if (ret)
		return;

	count = 0;
	while (count < USB_MAX_PACKET_SIZE) {
		int val = 0;

		if (!QUEUE_REMOVE_UNITS(&tx_q, &val, 1))
			break;

		if (!(count & 1))
			buf[count / 2] = val;
		else
			buf[count / 2] |= val << 8;
		count++;
	}

	if (count)
		usb_enable_tx(count);
}
DECLARE_DEFERRED(tx_fifo_handler);

static void handle_output(void)
{
	/* Wake up the Tx FIFO handler */
	hook_call_deferred(&tx_fifo_handler_data, 0);
}

/*
 * Public USB console implementation below.
 */
int usb_getc(void)
{
	int c = 0;

	if (!is_enabled)
		return -1;

	if (!QUEUE_REMOVE_UNITS(&rx_q, &c, 1))
		return -1;

	return c;
}

int usb_putc(int c)
{
	int ret;

	ret = __tx_char(NULL, c);
	handle_output();

	return ret;
}

int usb_puts(const char *outstr)
{
	/* Put all characters in the output buffer */
	while (*outstr) {
		if (__tx_char(NULL, *outstr++) != 0)
			break;
	}
	handle_output();

	/* Successful if we consumed all output */
	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int usb_vprintf(const char *format, va_list args)
{
	int ret;

	ret = vfnprintf(__tx_char, NULL, format, args);
	handle_output();

	return ret;
}

void usb_console_enable(int enabled, int readonly)
{
	is_enabled = enabled;
	is_readonly = readonly;
}

int usb_console_tx_blocked(void)
{
	return is_enabled && usb_console_tx_valid();
}
