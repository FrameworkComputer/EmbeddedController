/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* volteer family-specific USB-C configuration */

#ifndef __CROS_EC_BASEBOARD_USBC_CONFIG_H
#define __CROS_EC_BASEBOARD_USBC_CONFIG_H

#include "gpio_signal.h"

/* Common definition for the USB PD interrupt handlers. */
void ppc_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_BASEBOARD_USBC_CONFIG_H */
