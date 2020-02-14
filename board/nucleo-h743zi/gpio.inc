/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Note that these pins map to the Nucleo-H743ZI V2 and are only slightly
 * compatible with the original version.
 *
 * The V2 is denoted by "Nucleo-H743ZI2" vs. "Nucleo-H743ZI".
 */

#include "base-gpio.inc"

/* Interrupts */
GPIO_INT(PCH_SLP_S0_L,    PIN(D, 13), GPIO_INT_BOTH, slp_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(A, 11), GPIO_INT_BOTH, slp_event)
