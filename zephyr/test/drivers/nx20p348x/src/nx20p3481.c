/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/nx20p348x.h"
#include "nx20p348x_test_shared.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usbc_ppc.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST_F(nx20p348x_driver, test_sink_enable)
{
	uint8_t reg;

	zassert_ok(ppc_vbus_sink_enable(TEST_PORT, true));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_SWITCH_CONTROL_REG);
	zassert_equal(reg & NX20P3481_SWITCH_CONTROL_HVSNK,
		      NX20P3481_SWITCH_CONTROL_HVSNK);

	zassert_ok(ppc_vbus_sink_enable(TEST_PORT, false));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_SWITCH_CONTROL_REG);
	zassert_not_equal(reg & NX20P3481_SWITCH_CONTROL_HVSNK,
			  NX20P3481_SWITCH_CONTROL_HVSNK);
}

ZTEST_F(nx20p348x_driver, test_source_enable)
{
	uint8_t reg;

	zassert_ok(ppc_vbus_source_enable(TEST_PORT, true));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_SWITCH_CONTROL_REG);
	zassert_equal(reg & NX20P3481_SWITCH_CONTROL_5VSRC,
		      NX20P3481_SWITCH_CONTROL_5VSRC);

	zassert_ok(ppc_vbus_source_enable(TEST_PORT, false));
	reg = nx20p348x_emul_peek(fixture->nx20p348x_emul,
				  NX20P348X_SWITCH_CONTROL_REG);
	zassert_not_equal(reg & NX20P3481_SWITCH_CONTROL_5VSRC,
			  NX20P3481_SWITCH_CONTROL_5VSRC);
}
