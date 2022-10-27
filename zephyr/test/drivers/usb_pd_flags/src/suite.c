/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest_test_new.h>

#include "suite.h"

DEFINE_FAKE_VALUE_FUNC(int, board_vbus_source_enabled, int);
DEFINE_FAKE_VALUE_FUNC(int, ppc_discharge_vbus, int, int);

static void usb_pd_flags_after(void *fixture)
{
	RESET_FAKE(board_vbus_source_enabled);
	RESET_FAKE(ppc_discharge_vbus);
}

/* TODO(b/255413715): Convert this suite to use the unit testing framework when
 * practical.
 */
ZTEST_SUITE(usb_pd_flags, NULL, NULL, usb_pd_flags_after, NULL, NULL);
