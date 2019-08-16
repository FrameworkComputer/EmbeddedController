/* -*- mode:c -*-
 *
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(ACCEL_GYRO_INT_L,	PIN(0), GPIO_INT_FALLING, lsm6dsm_interrupt)
GPIO_INT(LID_OPEN,		PIN(5), GPIO_INT_BOTH, lid_interrupt)   /* LID_CL_NB_L */
GPIO_INT(TABLET_MODE_L,		PIN(6), GPIO_INT_BOTH, gmr_tablet_switch_isr) /* LID_CL_TAB_L */

GPIO(NB_MODE_L,			PIN(4), GPIO_OUT_LOW)

/*
 * We don't have a ENTERING_RW signal wired to the cr50 but common code needs
 * it to be defined.
 */
UNIMPLEMENTED(ENTERING_RW)

/*
 * SDA and SCL gpio must be set correctly in coreboot gpio
 */
