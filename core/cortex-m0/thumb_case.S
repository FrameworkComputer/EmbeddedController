/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Thumb mode toolchain helpers for compact switch/case statement.
 */

#include "config.h"

.text

.syntax unified
.code 16

/*
 * Helper for compact switch
 *
 * r0: the table index
 * lr: the table base address
 *
 * r0 and lr must be PRESERVED.
 * r12 can be clobbered.
 */
.global __gnu_thumb1_case_uqi
.thumb_func
__gnu_thumb1_case_uqi:
        push    {r1}
        mov     r1, lr
        lsrs    r1, r1, #1
        lsls    r1, r1, #1
        ldrb    r1, [r1, r0]
        lsls    r1, r1, #1
        add     lr, lr, r1
        pop     {r1}
        bx      lr
