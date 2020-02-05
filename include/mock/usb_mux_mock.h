/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock USB Type-C mux */

#include "usb_mux.h"

/* Controller for mux state */
struct mock_usb_mux_ctrl {
	mux_state_t state;
	int num_set_calls;
};

/* Resets the state of the mock */
void mock_usb_mux_reset(void);

extern const struct usb_mux_driver mock_usb_mux_driver;
extern struct mock_usb_mux_ctrl mock_usb_mux;
