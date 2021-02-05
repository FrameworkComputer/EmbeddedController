/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* brya family-specific USB-C configuration */

#ifndef __CROS_EC_BASEBOARD_USBC_CONFIG_H
#define __CROS_EC_BASEBOARD_USBC_CONFIG_H

#include "gpio_signal.h"

/* Common definition for the USB PD interrupt handlers. */
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void retimer_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);

#endif /* __CROS_EC_BASEBOARD_USBC_CONFIG_H */
