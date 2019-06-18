/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB endpoints/interfaces callbacks declaration
 */

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "common.h"
#include "usb_hw.h"

typedef void (*xfer_func)(void);
typedef void (*evt_func) (enum usb_ep_event evt);

#if defined(CHIP_FAMILY_STM32F4)
#define iface_arguments struct usb_setup_packet *req
#else
#define iface_arguments usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx
#endif
typedef int (*iface_func)(iface_arguments);

#ifndef PASS
#define PASS 1
#endif

#if PASS == 1
void ep_undefined(void)
{
	return;
}

void ep_evt_undefined(enum usb_ep_event evt)
{
	return;
}

/* Undefined interface callbacks fail by returning non-zero*/
int iface_undefined(iface_arguments)
{
	return 1;
}

#define table(type, name, x) x

#define endpoint_tx(number) \
	extern void __attribute__((used, weak, alias("ep_undefined"))) \
		ep_ ## number ## _tx(void);
#define endpoint_rx(number) \
	extern void __attribute__((used, weak, alias("ep_undefined"))) \
		ep_ ## number ## _rx(void);
#define endpoint_evt(number) \
	extern void __attribute__((used, weak, alias("ep_evt_undefined"))) \
		ep_ ## number ## _evt(enum usb_ep_event evt);
#define interface(number) \
	extern int __attribute__((used, weak, alias("iface_undefined"))) \
		iface_ ## number ## _request(iface_arguments);

#define null

#endif /* PASS 1 */

#if PASS == 2
#undef table
#undef endpoint_tx
#undef endpoint_rx
#undef endpoint_evt
#undef interface
#undef null

/* align function pointers on a 32-bit boundary */
#define table(type, name, x) type name[] __attribute__((aligned(4), section(".rodata.usb_ep." #name ",\"a\" @"))) = { x };
#define null (void*)0

#define ep_(num, suf) CONCAT3(ep_, num, suf)
#define ep(num, suf) ep_(num, suf)

#define endpoint_tx(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _tx,
#define endpoint_rx(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _rx,
#define endpoint_evt(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _evt,
#define interface(number) \
	[number < USB_IFACE_COUNT ? number : USB_IFACE_COUNT - 1] = iface_ ## number ## _request,
#endif /* PASS 2 */

/*
 * The initializers are listed backwards, but that's so that the items beyond
 * the chip's limit are first assigned to the last field, then overwritten by
 * its actual value due to the designated initializers in the macros above.
 * It all sorts out nicely
 */
table(xfer_func, usb_ep_tx,
	endpoint_tx(15)
	endpoint_tx(14)
	endpoint_tx(13)
	endpoint_tx(12)
	endpoint_tx(11)
	endpoint_tx(10)
	endpoint_tx(9)
	endpoint_tx(8)
	endpoint_tx(7)
	endpoint_tx(6)
	endpoint_tx(5)
	endpoint_tx(4)
	endpoint_tx(3)
	endpoint_tx(2)
	endpoint_tx(1)
	endpoint_tx(0)
)

table(xfer_func, usb_ep_rx,
	endpoint_rx(15)
	endpoint_rx(14)
	endpoint_rx(13)
	endpoint_rx(12)
	endpoint_rx(11)
	endpoint_rx(10)
	endpoint_rx(9)
	endpoint_rx(8)
	endpoint_rx(7)
	endpoint_rx(6)
	endpoint_rx(5)
	endpoint_rx(4)
	endpoint_rx(3)
	endpoint_rx(2)
	endpoint_rx(1)
	endpoint_rx(0)
)

table(evt_func, usb_ep_event,
	endpoint_evt(15)
	endpoint_evt(14)
	endpoint_evt(13)
	endpoint_evt(12)
	endpoint_evt(11)
	endpoint_evt(10)
	endpoint_evt(9)
	endpoint_evt(8)
	endpoint_evt(7)
	endpoint_evt(6)
	endpoint_evt(5)
	endpoint_evt(4)
	endpoint_evt(3)
	endpoint_evt(2)
	endpoint_evt(1)
	endpoint_evt(0)
)

#if USB_IFACE_COUNT > 0
table(iface_func, usb_iface_request,
	interface(7)
	interface(6)
	interface(5)
	interface(4)
	interface(3)
	interface(2)
	interface(1)
	interface(0)
)
#endif

#if PASS == 1
#undef PASS
#define PASS 2
#include "usb_endpoints.c"
#endif
