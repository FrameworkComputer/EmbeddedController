/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_HW_H
#define __CROS_EC_USB_HW_H

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
		__attribute__ ((alias(STRINGIFY(rst_handler))));

/* arrays with all endpoint callbacks */
extern void (*usb_ep_tx[]) (void);
extern void (*usb_ep_rx[]) (void);
extern void (*usb_ep_reset[]) (void);
/* array with interface-specific control request callbacks */
extern int (*usb_iface_request[]) (uint8_t *ep0_buf_rx, uint8_t *ep0_buf_tx);

#define _IFACE_HANDLER(num) CONCAT3(iface_, num, _request)
#define USB_DECLARE_IFACE(num, handler)					\
	int _IFACE_HANDLER(num)(uint8_t *ep0_buf_rx,			\
				uint8_t *epo_buf_tx)			\
	__attribute__ ((alias(STRINGIFY(handler))));

#endif	/* __CROS_EC_USB_HW_H */
