/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_realtek_rts54xx.h"
#include "i2c.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_pdc_api, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)
#define EMUL_DATA rts5453p_emul_get_i2c_common_data(EMUL)

const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
const struct device *dev = DEVICE_DT_GET(RTS5453P_NODE);

ZTEST_SUITE(pdc_api, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_api, test_get_ucsi_version)
{
	uint16_t version = 0;

	zassert_not_ok(pdc_get_ucsi_version(dev, NULL));

	zassert_ok(pdc_get_ucsi_version(dev, &version));
	zassert_equal(version, UCSI_VERSION);
}

ZTEST_USER(pdc_api, test_reset)
{
	/* TODO - why is PDC in IRQ state upon startup?
	 * zassert_ok(pdc_reset(dev), "Failed to reset PDC");
	 */
}
