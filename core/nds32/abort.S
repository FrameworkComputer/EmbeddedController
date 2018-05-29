/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * abort() handler
 */

#include "config.h"
#define PSW_INTL_SHIFT	1        /* Interrupt Stack Level */
#define PSW_INTL_MASK	(0x3 << PSW_INTL_SHIFT)

.text

.global abort
abort:
	movi55 $r6, 3 // assert
	movi55 $r7, 0 // no info. doubles for the equality test below

	// in interrupt context -> $r5 != 0
	mfsr $r5, $PSW
	andi $r5, $r5, PSW_INTL_MASK

	beq $r5, $r7, .L1
	j excep_handler
.L1:
	break 0
