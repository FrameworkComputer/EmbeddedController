/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base-gpio.inc"

/* Interrupts */
GPIO_INT(PCH_SLP_S0_L,    PIN(A, 8), GPIO_INT_BOTH, slp_event)
GPIO_INT(PCH_SLP_S3_L,    PIN(B, 6), GPIO_INT_BOTH, slp_event)
