/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power.h"

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/* Structure used by usb_mux test. It is part of usb_muxes chain. */
extern struct usb_mux usbc1_virtual_usb_mux;

void set_mock_power_state(enum power_state state);
