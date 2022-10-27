/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "cros_board_info.h"
#include "test/drivers/test_state.h"

ZTEST(cbi_gpio, test_cbio_is_write_protected)
{
	zassert_true(cbi_config.drv->is_protected());
}

ZTEST_SUITE(cbi_gpio, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
