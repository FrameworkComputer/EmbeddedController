/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#ifndef __CROS_EC_BASEBOARD_USBC_CONFIG_H
#define __CROS_EC_BASEBOARD_USBC_CONFIG_H

#include "gpio.h"

void ppc_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_BASEBOARD_USBC_CONFIG_H */
