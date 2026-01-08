/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <zephyr/devicetree.h>

/* Set the active keyboard configuration, called by vivaldi_kbd.c
 * initialization if more than one configurations are defined.
 */
int8_t board_vivaldi_keybd_idx(void);

/* Returns true if the specified row, col is the one specified for the
 * TK_VOL_UP key.
 */
bool vivaldi_kbd_is_vol_up(uint8_t row, uint8_t col);

#define VIVALDI_CFG_IDX(nodelabel) DT_NODE_CHILD_IDX(DT_NODELABEL(nodelabel))
