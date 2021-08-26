/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#include "gpio.h"

void tcpc_alert_event(enum gpio_signal signal);
void usb0_evt(enum gpio_signal signal);
void usb1_evt(enum gpio_signal signal);
void usba_oc_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_USBC_CONFIG_H */
