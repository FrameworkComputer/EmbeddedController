/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "registers.h"

#if !(DEBUG_CEC)
#define CPRINTF(...)
#define CPRINTS(...)
#else
#define CPRINTF(format, args...) cprintf(CC_CEC, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ## args)
#endif

static void cec_init(void)
{
	CPRINTS("CEC initialized");
}
DECLARE_HOOK(HOOK_INIT, cec_init, HOOK_PRIO_LAST);
