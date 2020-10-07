/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Mock for DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#include "usb_dp_alt_mode.h"
#include "mock/dp_alt_mode_mock.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

void mock_dp_alt_mode_reset(void)
{
	/* Nothing to do right now, but in the future ... */
}

void dp_init(int port)
{
	CPRINTS("C%d: DP init", port);
}

void dp_teardown(int port)
{
	CPRINTS("C%d: DP teardown", port);
}

