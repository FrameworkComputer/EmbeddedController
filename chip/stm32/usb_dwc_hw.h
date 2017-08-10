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
#define _EP_EVENT_HANDLER(num) _EP_HANDLER2(num, _evt)
/* Used to check function types are correct (attribute alias does not do it) */
#define _EP_TX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _tx_typecheck)
#define _EP_RX_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _rx_typecheck)
#define _EP_EVENT_HANDLER_TYPECHECK(num) _EP_HANDLER2(num, _evt_typecheck)

#define USB_DECLARE_EP(num, tx_handler, rx_handler, evt_handler)  \
	void _EP_TX_HANDLER(num)(void)				  \
		__attribute__ ((alias(STRINGIFY(tx_handler))));	  \
	void _EP_RX_HANDLER(num)(void)                            \
		__attribute__ ((alias(STRINGIFY(rx_handler))));	  \
	void _EP_EVENT_HANDLER(num)(enum usb_ep_event evt)	  \
		__attribute__ ((alias(STRINGIFY(evt_handler))));  \
	static __unused void					  \
	(*_EP_TX_HANDLER_TYPECHECK(num))(void) = tx_handler;	  \
	static __unused void					  \
	(*_EP_RX_HANDLER_TYPECHECK(num))(void) = rx_handler;	  \
	static __unused void					  \
	(*_EP_EVENT_HANDLER_TYPECHECK(num))(enum usb_ep_event evt)\
			= evt_handler

/* Endpoint callbacks */
extern void (*usb_ep_tx[]) (void);
extern void (*usb_ep_rx[]) (void);
extern void (*usb_ep_event[]) (enum usb_ep_event evt);
struct usb_setup_packet;
/* EP0 Interface handler callbacks */
extern int (*usb_iface_request[]) (struct usb_setup_packet *req);


/* True if the HW Rx/OUT FIFO is currently listening. */
int rx_ep_is_active(uint32_t ep_num);

/* Number of bytes the HW Rx/OUT FIFO has for us.
 *
 * @param ep_num        USB endpoint
 *
 * @returns             number of bytes ready, zero if none.
 */
int rx_ep_pending(uint32_t ep_num);

/* True if the Tx/IN FIFO can take some bytes from us. */
int tx_ep_is_ready(uint32_t ep_num);

/* Write packets of data IN to the host.
 *
 * This function uses DMA, so the *data write buffer
 * must persist until the write completion event.
 *
 * @param ep_num        USB endpoint to write
 * @param len           number of bytes to write
 * @param data          pointer of data to write
 *
 * @return              bytes written
 */
int usb_write_ep(uint32_t ep_num, int len, void *data);

/* Read a packet of data OUT from the host.
 *
 * This function uses DMA, so the *data write buffer
 * must persist until the read completion event.
 *
 * @param ep_num        USB endpoint to read
 * @param len           number of bytes to read
 * @param data          pointer of data to read
 *
 * @return              EC_SUCCESS on success
 */
int usb_read_ep(uint32_t ep_num, int len, void *data);

/* Tx/IN interrupt handler */
void usb_epN_tx(uint32_t ep_num);

/* Rx/OUT endpoint interrupt handler */
void usb_epN_rx(uint32_t ep_num);

/* Reset endpoint HW block. */
void epN_reset(uint32_t ep_num);

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
