/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Set the active keyboard configuration, called by vivaldi_kbd.c
 * initialization if more than one configurations are defined.
 */
int8_t board_vivaldi_keybd_idx(void);

/* Returns true if the specified row, col is the one specified for the
 * TK_VOL_UP key.
 */
bool vivaldi_kbd_is_vol_up(uint8_t row, uint8_t col);
