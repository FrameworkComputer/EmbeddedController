/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BASEBOARD_BLOONCHIPPER_BASE_BOARD_RW_H
#define __CROS_EC_BASEBOARD_BLOONCHIPPER_BASE_BOARD_RW_H

#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

void fps_event(enum gpio_signal signal);

void board_init_rw(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BASEBOARD_BLOONCHIPPER_BASE_BOARD_RW_H */
