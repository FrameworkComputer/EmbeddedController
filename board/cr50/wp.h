/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

/**
 * Set the current write protect state in RBOX and long life scratch register.
 *
 * @param asserted: 0 to disable write protect, otherwise enable write protect.
 */
void set_wp_state(int asserted);
