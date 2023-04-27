/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "nx20p348x_test_shared.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usbc_ppc.h"

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(nx20p348x_driver, test_sink_enable_success)
{
	/* Note: PPC requires a TCPC GPIO to enable its sinking.
	 * We check if the TCPC's POWER_STATUS is set properly.
	 */
	zassert_equal(ppc_vbus_sink_enable(TEST_PORT, true), EC_SUCCESS);
}

ZTEST(nx20p348x_driver, test_source_enable_success)
{
	/* Note: PPC requires a TCPC GPIO to enable its sinking.
	 * We check if the TCPC's POWER_STATUS is set properly.
	 */
	zassert_equal(ppc_vbus_source_enable(TEST_PORT, true), EC_SUCCESS);
}

ZTEST_F(nx20p348x_driver, test_sink_enable_timeout_failure)
{
	nx20p348x_emul_set_tcpc_interact(fixture->nx20p348x_emul, false);
	/* Note: PPC requires a TCPC GPIO to enable its sinking */
	zassert_equal(ppc_vbus_sink_enable(TEST_PORT, true), EC_ERROR_TIMEOUT);
}

ZTEST_F(nx20p348x_driver, test_source_enable_timeout_failure)
{
	nx20p348x_emul_set_tcpc_interact(fixture->nx20p348x_emul, false);
	/* Note: PPC requires a TCPC GPIO to enable its sourcing */
	zassert_equal(ppc_vbus_source_enable(TEST_PORT, true),
		      EC_ERROR_TIMEOUT);
}
