/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power.h"

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/* Structure used by usb_mux test. It is part of usb_muxes chain. */
extern struct usb_mux usbc1_virtual_usb_mux;

void set_mock_power_state(enum power_state state);

/**
 * @brief Set product ID that should be returned by board_get_ps8xxx_product_id
 *
 * @param product_id ID of PS8xxx product which is emulated
 */
void board_set_ps8xxx_product_id(uint16_t product_id);
