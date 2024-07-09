/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CAPSENSE_H
#define __CROS_EC_CAPSENSE_H

#include "common.h"
#include "gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

void capsense_interrupt(enum gpio_signal signal);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CAPSENSE_H */
