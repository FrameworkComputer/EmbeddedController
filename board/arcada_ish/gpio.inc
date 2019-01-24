/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(NB_MODE_L,		PIN(4), GPIO_OUT_HIGH)
GPIO(LID_CL_NB_L,	PIN(5), GPIO_INPUT)
GPIO(LID_CL_TAB_L,	PIN(6), GPIO_INPUT)

/*
 * We don't have a ENTERING_RW signal wired to the cr50 but common code needs
 * it to be defined.
 */
UNIMPLEMENTED(ENTERING_RW)
