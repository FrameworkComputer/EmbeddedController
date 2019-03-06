/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB retimer driver */

#ifndef __CROS_EC_USB_RETIMER_H
#define __CROS_EC_USB_RETIMER_H

#include "usb_mux.h"

/**
 * Set USB retimer state
 *
 * @param port Port number.
 * @param mux_state current MUX state
 * @return Non-zero if fail; otherwise, zero.
 */
int retimer_set_state(int port, mux_state_t mux_state);

/**
 * USB retimer enter to low power mode.
 *
 * @param port Port number.
 * @return Non-zero if fail; otherwise, zero.
 */
int retimer_low_power_mode(int port);

/**
 * Initialize USB Retimer to its default state.
 *
 * @param port Port number.
 * @return Non-zero if fail; otherwise, zero.
 */
int retimer_init(int port);

#endif /* __CROS_EC_USB_RETIMER_H */
