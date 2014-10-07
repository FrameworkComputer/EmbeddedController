/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
/* TODO(crosbug.com/p/33432): try enabling this */
/* GPIO(CAMO0_BREACH_INT, A, 0, GPIO_INT_BOTH, button_event) */

/* Outputs */

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)

/* Alternate functions */
