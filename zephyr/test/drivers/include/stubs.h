/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TEST_DRIVERS_STUBS_H
#define __TEST_DRIVERS_STUBS_H

#include "power.h"

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

/* Structure used by usb_mux test. It is part of usb_muxes chain. */
extern struct usb_mux usbc1_virtual_usb_mux;

/**
 * @brief Set state which should be returned by power_handle_state() and wake
 *        chipset task to immediately change state
 *
 * @param force If true @p state will be used as return for power_handle_state()
 *              and will wake up chipset task. If false argument of
 *              power_handle_state() will be used as return value
 * @param state Power state to use when @p force is true
 */
void force_power_state(bool force, enum power_state state);

/**
 * @brief Set product ID that should be returned by board_get_ps8xxx_product_id
 *
 * @param product_id ID of PS8xxx product which is emulated
 */
void board_set_ps8xxx_product_id(uint16_t product_id);

#endif /* __TEST_DRIVERS_STUBS_H */
