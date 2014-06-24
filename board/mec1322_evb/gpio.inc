/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(LED1,       PORT(15), 4, GPIO_ODR_LOW,  NULL)
GPIO(LED2,       PORT(15), 5, GPIO_ODR_HIGH, NULL)
GPIO(LED3,       PORT(15), 6, GPIO_ODR_LOW,  NULL)
GPIO(PCH_SMI_L,  PORT(4),  4, GPIO_ODR_HIGH, NULL) /* SMI output */
GPIO(PCH_WAKE_L, PORT(20), 0, GPIO_ODR_HIGH, NULL) /* PCH wake pin */

/* Switch S1 */
GPIO(S1,         PORT(6),  3, GPIO_INT_FALLING | GPIO_PULL_UP, NULL)

/*
 * Signals which aren't implemented on MEC1322 eval board but we'll
 * emulate anyway, to make it more convenient to debug other code.
 */
UNIMPLEMENTED(RECOVERY_L)  /* Recovery signal from DOWN button */
UNIMPLEMENTED(WP)          /* Write protect input */
UNIMPLEMENTED(ENTERING_RW) /* EC entering RW code */
