/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_NOCTURNE_FP_BOARD_RW_H
#define __CROS_EC_BOARD_NOCTURNE_FP_BOARD_RW_H

void fps_event(enum gpio_signal signal);

/* Defined in ro_workarounds.c */
void wp_event(enum gpio_signal signal);

#endif /* __CROS_EC_BOARD_NOCTURNE_FP_BOARD_RW_H */
