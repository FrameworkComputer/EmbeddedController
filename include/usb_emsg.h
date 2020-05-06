/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Extended message buffer */

#ifndef __CROS_EC_USB_EBUF_H
#define __CROS_EC_USB_EBUF_H

#ifdef CONFIG_USB_PD_REV30
#define EXTENDED_BUFFER_SIZE 260
#else
#define EXTENDED_BUFFER_SIZE 28
#endif

struct extended_msg {
	uint32_t header;
	uint32_t len;
	uint8_t buf[EXTENDED_BUFFER_SIZE];
};

/* Defined in usb_prl_sm.c */
extern struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
extern struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif /* __CROS_EC_USB_EBUF_H */
