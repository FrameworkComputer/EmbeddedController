/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* lazor board-specific USB-C configuration */

#ifndef __ZEPHYR_LAZOR_USBC_CONFIG_H
#define __ZEPHYR_LAZOR_USBC_CONFIG_H

#include "gpio.h"

void tcpc_alert_event(enum gpio_signal signal);
void usb0_evt(enum gpio_signal signal);
void usb1_evt(enum gpio_signal signal);
void usba_oc_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void board_connect_c0_sbu(enum gpio_signal s);

#endif /* __ZEPHYR_LAZOR_USBC_CONFIG_H */
