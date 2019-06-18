/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB endpoints/interfaces callbacks declaration
 */

#include "config.h"
#include "common.h"
#include "usb_hw.h"

typedef void (*xfer_func)(void);
typedef void (*rst_func) (void);
typedef int (*iface_func)(struct usb_setup_packet *req);
#ifndef PASS
#define PASS 1
#endif

#if PASS == 1
void ep_undefined(void)
{
	return;
}

void ep_rst_undefined(void)
{
	return;
}

/* Undefined interface callbacks fail by returning non-zero*/
int iface_undefined(struct usb_setup_packet *req)
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
#define endpoint_rst(number) \
	extern void __attribute__((used, weak, alias("ep_rst_undefined"))) \
		ep_ ## number ## _rst(void);
#define interface(number) \
	extern int __attribute__((used, weak, alias("iface_undefined"))) \
		iface_ ## number ## _request(struct usb_setup_packet *req);

#define null

#endif /* PASS 1 */

#if PASS == 2
#undef table
#undef endpoint_tx
#undef endpoint_rx
#undef endpoint_rst
#undef interface
#undef null

/* align function pointers on a 32-bit boundary */
#define table(type, name, x) type name[] __attribute__((aligned(4),section(".rodata.usb_ep." #name ",\"a\" @"))) = { x };
#define null (void *)0

#define ep_(num, suf) CONCAT3(ep_, num, suf)
#define ep(num, suf) ep_(num, suf)

#define endpoint_tx(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _tx,
#define endpoint_rx(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _rx,
#define endpoint_rst(number) \
	[number < USB_EP_COUNT ? number : USB_EP_COUNT - 1] = ep_ ## number ## _rst,
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

table(rst_func, usb_ep_reset,
	endpoint_rst(15)
	endpoint_rst(14)
	endpoint_rst(13)
	endpoint_rst(12)
	endpoint_rst(11)
	endpoint_rst(10)
	endpoint_rst(9)
	endpoint_rst(8)
	endpoint_rst(7)
	endpoint_rst(6)
	endpoint_rst(5)
	endpoint_rst(4)
	endpoint_rst(3)
	endpoint_rst(2)
	endpoint_rst(1)
	endpoint_rst(0)
)

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

#if PASS == 1
#undef PASS
#define PASS 2
#include "usb_endpoints.c"
#endif
