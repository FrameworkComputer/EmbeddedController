/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_DWC_HW_H
#define __CROS_EC_USB_DWC_HW_H

#include "usb_dwc_registers.h"

/* Helpers for endpoint declaration */
#define _EP_HANDLER2(num, suffix) CONCAT3(ep_, num, suffix)
#define _EP_TX_HANDLER(num) _EP_HANDLER2(num, _tx)
#define _EP_RX_HANDLER(num) _EP_HANDLER2(num, _rx)
#define _EP_RESET_HANDLER(num) _EP_HANDLER2(num, _rst)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, rst_handler)  \
	void _EP_TX_HANDLER(num)(void)				  \
		__attribute__ ((alias(STRINGIFY(tx_handler))));	  \
	void _EP_RX_HANDLER(num)(void)                            \
		__attribute__ ((alias(STRINGIFY(rx_handler))));	  \
	void _EP_RESET_HANDLER(num)(void)                         \
		__attribute__ ((alias(STRINGIFY(rst_handler))))

/* Endpoint callbacks */
extern void (*usb_ep_tx[]) (void);
extern void (*usb_ep_rx[]) (void);
extern void (*usb_ep_reset[]) (void);
struct usb_setup_packet;
/* EP0 Interface handler callbacks */
static int (*usb_iface_request[]) (struct usb_setup_packet *req);

/*
 * Declare any interface-specific control request handlers. These Setup packets
 * arrive on the control endpoint (EP0), but are handled by the interface code.
 * The callback must prepare the EP0 IN or OUT FIFOs and return the number of
 * bytes placed in the IN FIFO. A negative return value will STALL the response
 * (and thus indicate error to the host).
 */
#define _IFACE_HANDLER(num) CONCAT3(iface_, num, _request)
#define USB_DECLARE_IFACE(num, handler)				\
	int _IFACE_HANDLER(num)(struct usb_setup_packet *req)	\
		__attribute__ ((alias(STRINGIFY(handler))))

#endif	/* __CROS_EC_USB_DWC_HW_H */
