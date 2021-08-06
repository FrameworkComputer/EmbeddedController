/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_HATCH_FP_BOARD_RW_H
#define __CROS_EC_BOARD_HATCH_FP_BOARD_RW_H

void fps_event(enum gpio_signal signal);
void slp_event(enum gpio_signal signal);

void board_init_rw(void);

#endif /* __CROS_EC_BOARD_HATCH_FP_BOARD_RW_H */
