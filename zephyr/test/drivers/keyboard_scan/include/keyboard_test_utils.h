/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Press or release a key through the keyboard emulator
 *
 * @param row Key row
 * @param col Key column
 * @param pressed 1 if pressed, 0 otherwise
 * @return int 0 if successful
 */
int emulate_keystate(int row, int col, int pressed);

/**
 * @brief Clears any pressed keys in the keyboard emulator
 */
void clear_emulated_keys(void);
