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
#define USB_CONSOLE_RX_BUF_SIZE 16
#define RX_BUF_NEXT(i) (((i) + 1) & (USB_CONSOLE_RX_BUF_SIZE - 1))

static volatile char rx_buf[USB_CONSOLE_RX_BUF_SIZE];
static volatile int rx_buf_head;
static volatile int rx_buf_tail;

static int last_tx_ok = 1;

static int is_reset;
static int is_enabled = 1;
static int is_readonly;

/* USB-Serial descriptors */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_CONSOLE) =
{
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
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 0) =
{
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = 0x80 | USB_EP_CONSOLE,
	.bmAttributes       = 0x02 /* Bulk IN */,
	.wMaxPacketSize     = USB_MAX_PACKET_SIZE,
	.bInterval          = 10
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 1) =
{
	.bLength            = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType    = USB_DT_ENDPOINT,
	.bEndpointAddress   = USB_EP_CONSOLE,
	.bmAttributes       = 0x02 /* Bulk OUT */,
	.wMaxPacketSize     = USB_MAX_PACKET_SIZE,
	.bInterval          = 0
};

static uint8_t ep_buf_tx[USB_MAX_PACKET_SIZE];
static uint8_t ep_buf_rx[USB_MAX_PACKET_SIZE];
static struct g_usb_desc ep_out_desc;
static struct g_usb_desc ep_in_desc;

static void con_ep_tx(void)
{
	/* clear IT */
	GR_USB_DIEPINT(USB_EP_CONSOLE) = 0xffffffff;
}

static void con_ep_rx(void)
{
	int i;
	int rx_size = is_readonly ? 0 : USB_MAX_PACKET_SIZE
		    - (ep_out_desc.flags & DOEPDMA_RXBYTES_MASK);

	for (i = 0; i < rx_size; i++) {
		int rx_buf_next = RX_BUF_NEXT(rx_buf_head);
		if (rx_buf_next != rx_buf_tail) {
			rx_buf[rx_buf_head] = ep_buf_rx[i];
			rx_buf_head = rx_buf_next;
		}
	}

	ep_out_desc.flags = DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE) |
			    DOEPDMA_LAST | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
	GR_USB_DOEPCTL(USB_EP_CONSOLE) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
	/* clear IT */
	GR_USB_DOEPINT(USB_EP_CONSOLE) = 0xffffffff;

	/* wake-up the console task */
	if (!is_readonly)
		console_has_input();
}

static void ep_reset(void)
{
	ep_out_desc.flags = DOEPDMA_RXBYTES(USB_MAX_PACKET_SIZE) |
			    DOEPDMA_LAST | DOEPDMA_BS_HOST_RDY | DOEPDMA_IOC;
	ep_out_desc.addr = ep_buf_rx;
	GR_USB_DOEPDMA(USB_EP_CONSOLE) = (uint32_t)&ep_out_desc;
	ep_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_BSY | DIEPDMA_IOC;
	ep_in_desc.addr = ep_buf_tx;
	GR_USB_DIEPDMA(USB_EP_CONSOLE) = (uint32_t)&ep_in_desc;
	GR_USB_DOEPCTL(USB_EP_CONSOLE) = DXEPCTL_MPS(64) | DXEPCTL_USBACTEP |
					 DXEPCTL_EPTYPE_BULK |
					 DXEPCTL_CNAK | DXEPCTL_EPENA;
	GR_USB_DIEPCTL(USB_EP_CONSOLE) = DXEPCTL_MPS(64) | DXEPCTL_USBACTEP |
					 DXEPCTL_EPTYPE_BULK |
					 DXEPCTL_TXFNUM(USB_EP_CONSOLE);
	GR_USB_DAINTMSK |= (1<<USB_EP_CONSOLE) | (1 << (USB_EP_CONSOLE+16));

	is_reset = 1;
}

USB_DECLARE_EP(USB_EP_CONSOLE, con_ep_tx, con_ep_rx, ep_reset);

static int __tx_char(void *context, int c)
{
	int *tx_idx = context;

	/* Do newline to CRLF translation */
	if (c == '\n' && __tx_char(context, '\r'))
		return 1;

	if (*tx_idx > 63)
		return 1;

	ep_buf_tx[*tx_idx] = c;
	(*tx_idx)++;

	return 0;
}

static void usb_enable_tx(int len)
{
	if (!is_enabled)
		return;

	ep_in_desc.flags = DIEPDMA_LAST | DIEPDMA_BS_HOST_RDY | DIEPDMA_IOC |
			   DIEPDMA_TXBYTES(len);
	GR_USB_DIEPCTL(USB_EP_CONSOLE) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

static inline int usb_console_tx_valid(void)
{
	return (ep_in_desc.flags & DIEPDMA_BS_MASK) == DIEPDMA_BS_DMA_DONE;
}

static int usb_wait_console(void)
{
	timestamp_t deadline = get_time();
	int wait_time_us = 1;

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

	ret = vfnprintf(__tx_char, &tx_idx, format, args);
	if (!ret && is_reset) {
		ret = usb_wait_console();
		if (!ret)
			usb_enable_tx(tx_idx);
	}
	return ret;
}

void usb_console_enable(int enabled, int readonly)
{
	is_enabled = enabled;
	is_readonly = readonly;
}
