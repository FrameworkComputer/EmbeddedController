/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Extended message buffer */

#ifndef __CROS_EC_USB_EBUF_H
#define __CROS_EC_USB_EBUF_H

#define EXTENDED_BUFFER_SIZE 260
#define BUFFER_SIZE 28

struct extended_msg {
	uint32_t header;
	uint32_t len;
	uint8_t buf[EXTENDED_BUFFER_SIZE];
};

/* Defined in usb_prl_sm.c */
extern struct extended_msg emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

#endif /* __CROS_EC_USB_EBUF_H */
