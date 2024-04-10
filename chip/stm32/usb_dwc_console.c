/* Copyright 2016 The ChromiumOS Authors
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
#include "usb_descriptor.h"
#include "usb_hw.h"
#include "util.h"

/* Console output macro */
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)
#define USB_CONSOLE_TIMEOUT_US (30 * MSEC)

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
	.bInterval = 10,
};
const struct usb_endpoint_descriptor USB_EP_DESC(USB_IFACE_CONSOLE, 1) = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_EP_CONSOLE,
	.bmAttributes = 0x02 /* Bulk OUT */,
	.wMaxPacketSize = USB_MAX_PACKET_SIZE,
	.bInterval = 0
};

static uint8_t ep_buf_tx[USB_MAX_PACKET_SIZE];
static uint8_t ep_buf_rx[USB_MAX_PACKET_SIZE];

static struct queue const tx_q = QUEUE_NULL(256, uint8_t);
static struct queue const rx_q = QUEUE_NULL(USB_MAX_PACKET_SIZE, uint8_t);

struct dwc_usb_ep ep_console_ctl = {
	.max_packet = USB_MAX_PACKET_SIZE,
	.tx_fifo = USB_EP_CONSOLE,
	.out_pending = 0,
	.out_data = 0,
	.out_databuffer = ep_buf_tx,
	.out_databuffer_max = sizeof(ep_buf_tx),
	.in_packets = 0,
	.in_pending = 0,
	.in_data = 0,
	.in_databuffer = ep_buf_rx,
	.in_databuffer_max = sizeof(ep_buf_rx),
};

/* Let the USB HW IN-to-host FIFO transmit some bytes */
static void usb_enable_tx(int len)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;

	ep->in_data = ep->in_databuffer;
	ep->in_packets = 1;
	ep->in_pending = len;

	GR_USB_DIEPTSIZ(USB_EP_CONSOLE) = 0;

	GR_USB_DIEPTSIZ(USB_EP_CONSOLE) |= DXEPTSIZ_PKTCNT(1);
	GR_USB_DIEPTSIZ(USB_EP_CONSOLE) |= DXEPTSIZ_XFERSIZE(len);
	GR_USB_DIEPDMA(USB_EP_CONSOLE) = (uint32_t)ep->in_data;

	GR_USB_DIEPCTL(USB_EP_CONSOLE) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

/* Let the USB HW OUT-from-host FIFO receive some bytes */
static void usb_enable_rx(int len)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;

	ep->out_data = ep->out_databuffer;
	ep->out_pending = 0;

	GR_USB_DOEPTSIZ(USB_EP_CONSOLE) = 0;
	GR_USB_DOEPTSIZ(USB_EP_CONSOLE) |= DXEPTSIZ_PKTCNT(1);
	GR_USB_DOEPTSIZ(USB_EP_CONSOLE) |= DXEPTSIZ_XFERSIZE(len);
	GR_USB_DOEPDMA(USB_EP_CONSOLE) = (uint32_t)ep->out_data;

	GR_USB_DOEPCTL(USB_EP_CONSOLE) |= DXEPCTL_CNAK | DXEPCTL_EPENA;
}

/* True if the HW Rx/OUT FIFO has bytes for us. */
static inline int rx_fifo_is_ready(void)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;

	return ep->out_pending;
}

/*
 * This function tries to shove new bytes from the USB host into the queue for
 * consumption elsewhere. It is invoked either by a HW interrupt (telling us we
 * have new bytes from the USB host), or by whoever is reading bytes out of the
 * other end of the queue (telling us that there's now more room in the queue
 * if we still have bytes to shove in there).
 */
char buffer[65];
static void rx_fifo_handler(void)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;

	int rx_in_fifo;
	size_t added;

	if (!rx_fifo_is_ready())
		return;

	rx_in_fifo = ep->out_pending;
	added = QUEUE_ADD_UNITS(&rx_q, ep->out_databuffer, rx_in_fifo);

	if (added != rx_in_fifo)
		CPRINTF("DROP CONSOLE: %d/%d process\n", added, rx_in_fifo);

	/* wake-up the console task */
	console_has_input();

	usb_enable_rx(USB_MAX_PACKET_SIZE);
}
DECLARE_DEFERRED(rx_fifo_handler);

/* Rx/OUT interrupt handler */
static void con_ep_rx(void)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;

	if (GR_USB_DOEPCTL(USB_EP_CONSOLE) & DXEPCTL_EPENA)
		return;

	/* Bytes received decrement DOEPTSIZ XFERSIZE */
	if (GR_USB_DOEPINT(USB_EP_CONSOLE) & DOEPINT_XFERCOMPL) {
		ep->out_pending =
			ep->max_packet - (GR_USB_DOEPTSIZ(USB_EP_CONSOLE) &
					  GC_USB_DOEPTSIZ1_XFERSIZE_MASK);
	}

	/* Wake up the Rx FIFO handler */
	hook_call_deferred(&rx_fifo_handler_data, 0);

	/* clear the RX/OUT interrupts */
	GR_USB_DOEPINT(USB_EP_CONSOLE) = 0xffffffff;
}

/* True if the Tx/IN FIFO can take some bytes from us. */
static inline int tx_fifo_is_ready(void)
{
	return !(GR_USB_DIEPCTL(USB_EP_CONSOLE) & DXEPCTL_EPENA);
}

/* Try to send some bytes to the host */
static void tx_fifo_handler(void)
{
	struct dwc_usb_ep *ep = &ep_console_ctl;
	size_t count;

	if (!is_reset)
		return;

	/* If the HW FIFO isn't ready, then we can't do anything right now. */
	if (!tx_fifo_is_ready())
		return;

	count = QUEUE_REMOVE_UNITS(&tx_q, ep->in_databuffer,
				   USB_MAX_PACKET_SIZE);
	if (count)
		usb_enable_tx(count);
}
DECLARE_DEFERRED(tx_fifo_handler);

static void handle_output(void)
{
	/* Wake up the Tx FIFO handler */
	hook_call_deferred(&tx_fifo_handler_data, 0);
}

/* Tx/IN interrupt handler */
static void con_ep_tx(void)
{
	/* Wake up the Tx FIFO handler */
	hook_call_deferred(&tx_fifo_handler_data, 0);

	/* clear the Tx/IN interrupts */
	GR_USB_DIEPINT(USB_EP_CONSOLE) = 0xffffffff;
}

static void ep_event(enum usb_ep_event evt)
{
	if (evt != USB_EVENT_RESET)
		return;

	epN_reset(USB_EP_CONSOLE);

	is_reset = 1;

	/* Flush any queued data */
	hook_call_deferred(&tx_fifo_handler_data, 0);
	hook_call_deferred(&rx_fifo_handler_data, 0);

	usb_enable_rx(USB_MAX_PACKET_SIZE);
}

USB_DECLARE_EP(USB_EP_CONSOLE, con_ep_tx, con_ep_rx, ep_event);

static int usb_wait_console(void)
{
	timestamp_t deadline = get_time();
	int wait_time_us = 1;

	if (!is_enabled || !tx_fifo_is_ready())
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
		while (queue_space(&tx_q) < USB_MAX_PACKET_SIZE || !is_reset) {
			if (timestamp_expired(deadline, NULL) ||
			    in_interrupt_context()) {
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
	}

	last_tx_ok = queue_space(&tx_q);
	return EC_SUCCESS;
}
static int __tx_char(void *context, int c)
{
	struct queue *state = (struct queue *)context;

	if (c == '\n' && __tx_char(state, '\r'))
		return 1;

	QUEUE_ADD_UNITS(state, &c, 1);
	return 0;
}

/*
 * Public USB console implementation below.
 */
int usb_getc(void)
{
	int c;

	if (!is_enabled)
		return -1;

	if (QUEUE_REMOVE_UNITS(&rx_q, &c, 1))
		return c;

	return -1;
}

int usb_puts(const char *outstr)
{
	int ret;
	struct queue state;

	if (is_readonly)
		return EC_SUCCESS;

	ret = usb_wait_console();
	if (ret)
		return ret;

	state = tx_q;
	while (*outstr)
		if (__tx_char(&state, *outstr++))
			break;

	if (queue_count(&state))
		handle_output();

	return *outstr ? EC_ERROR_OVERFLOW : EC_SUCCESS;
}

int usb_putc(int c)
{
	char string[2];

	string[0] = c;
	string[1] = '\0';
	return usb_puts(string);
}

int usb_vprintf(const char *format, va_list args)
{
	int ret;
	struct queue state;

	if (is_readonly)
		return EC_SUCCESS;

	ret = usb_wait_console();
	if (ret)
		return ret;

	state = tx_q;
	ret = vfnprintf(__tx_char, &state, format, args);

	if (queue_count(&state))
		handle_output();

	return ret;
}

void usb_console_enable(int enabled, int readonly)
{
	is_enabled = enabled;
	is_readonly = readonly;
}
