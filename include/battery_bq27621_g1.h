/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for BQ27621-G1
 */

/* Sets percent to the battery life as a percentage (0-100)
 *
 * Returns EC_SUCCESS on success.
 */
int bq27621_state_of_charge(int *percent);

/* Initializes the fuel gauge with the constants for the battery.
 *
 * Returns EC_SUCCESS on success.
 */
int bq27621_init(void);

