/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_realtek_rts54xx_public.h"
#include "i2c.h"
#include "pdc_trace_msg.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_rts54xx, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)

static const uint32_t epr_pdos[] = {
	PDO_AUG_EPR(5000, 20000, 140, 0), PDO_AUG_EPR(5000, 20000, 140, 0),
	PDO_AUG_EPR(5000, 20000, 140, 0), PDO_AUG_EPR(5000, 20000, 140, 0),
	PDO_AUG_EPR(5000, 20000, 140, 0),
};
static const uint32_t spr_pdos[] = {
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(5000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(9000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(15000, 3000, 0),
	PDO_AUG(1000, 5000, 3000), PDO_FIXED(20000, 3000, 0),
};
static const uint32_t mixed_pdos_success[] = {
	PDO_AUG_EPR(5000, 20000, 140, 0),
	PDO_FIXED(5000, 3000, PDO_FIXED_EPR_MODE_CAPABLE),
	PDO_AUG(1000, 5000, 3000),
	PDO_FIXED(5000, 3000, 0),
	PDO_FIXED(9000, 3000, 0),
	PDO_FIXED(20000, 3000, 0),
};
static const uint32_t mixed_pdos_failure[] = {
	PDO_AUG(1000, 5000, 3000),
	PDO_FIXED(5000, 3000, 0),
	PDO_FIXED(9000, 3000, 0),
	PDO_FIXED(20000, 3000, 0),
	PDO_FIXED(5000, 3000, PDO_FIXED_EPR_MODE_CAPABLE),
	PDO_AUG_EPR(5000, 20000, 140, 0),
};

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
static const struct device *dev = DEVICE_DT_GET(RTS5453P_NODE);

static void rts54xx_before_test(void *data)
{
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}
}

static int emul_get_src_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     uint32_t *pdos)
{
	return emul_pdc_get_pdos(emul, SOURCE_PDO, pdo_offset, pdo_count, false,
				 pdos);
}

static int emul_get_snk_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     uint32_t *pdos)
{
	return emul_pdc_get_pdos(emul, SINK_PDO, pdo_offset, pdo_count, false,
				 pdos);
}

static int emul_set_src_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     const uint32_t *pdos)
{
	return emul_pdc_set_pdos(emul, SOURCE_PDO, pdo_offset, pdo_count, pdos);
}

static int emul_set_snk_pdos(enum pdo_offset_t pdo_offset, uint8_t pdo_count,
			     const uint32_t *pdos)
{
	return emul_pdc_set_pdos(emul, SINK_PDO, pdo_offset, pdo_count, pdos);
}

ZTEST_SUITE(rts54xx, NULL, NULL, rts54xx_before_test, NULL, NULL);

ZTEST_USER(rts54xx, test_emul_reset)
{
	uint32_t pdos[PDO_OFFSET_MAX];

	/* Test source PDO reset values. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_0, 8, pdos));
	zassert_equal(pdos[0], RTS5453P_FIXED_SRC);

	for (int i = 0; i < 7; i++) {
		zassert_equal(pdos[i + 1], 0xFFFFFFFF);
	}

	/* Test sink PDO reset values. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_0, 8, pdos));
	zassert_equal(pdos[0], RTS5453P_FIXED_SNK);
	zassert_equal(pdos[1], RTS5453P_BATT_SNK);
	zassert_equal(pdos[2], RTS5453P_VAR_SNK);

	for (int i = 3; i < 7; i++) {
		zassert_equal(pdos[i + 1], 0xFFFFFFFF);
	}
}

ZTEST_USER(rts54xx, test_emul_pdos)
{
	uint32_t pdos[PDO_OFFSET_MAX];

	/* Port partner PDOs aren't currently supported. */
	/* TODO b/317065172: Update when port partner functionality is in. */
	zassert_not_ok(emul_pdc_get_pdos(emul, SOURCE_PDO, PDO_OFFSET_0, 1,
					 true, pdos));
	zassert_not_ok(
		emul_pdc_get_pdos(emul, SINK_PDO, PDO_OFFSET_0, 1, true, pdos));

	/* Test that offset zero is invalid for setting. */
	zassert_not_ok(emul_set_src_pdos(PDO_OFFSET_0, 1, pdos));
	zassert_not_ok(emul_set_snk_pdos(PDO_OFFSET_0, 1, pdos));

	/* Test PDO overflow. */
	zassert_not_ok(emul_set_src_pdos(PDO_OFFSET_1, 8, spr_pdos));
	zassert_not_ok(emul_set_snk_pdos(PDO_OFFSET_1, 8, spr_pdos));

	zassert_not_ok(emul_get_src_pdos(PDO_OFFSET_5, 8, pdos));
	zassert_not_ok(emul_get_snk_pdos(PDO_OFFSET_5, 8, pdos));

	/* Test that only PDOs 1-4 support EPR. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET,
				     epr_pdos));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET,
				     pdos));
	zassert_ok(memcmp(pdos, epr_pdos, sizeof(uint32_t) * 4));
	zassert_not_ok(emul_set_src_pdos(
		PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET + 1, epr_pdos));

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_snk_pdos(PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET,
				     epr_pdos));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET,
				     pdos));
	zassert_ok(memcmp(pdos, epr_pdos, sizeof(uint32_t) * 4));
	zassert_not_ok(emul_set_snk_pdos(
		PDO_OFFSET_1, RTS5453P_MAX_EPR_PDO_OFFSET + 1, epr_pdos));

	/* Test that SPR PDOs can be placed in any offset. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, 7, spr_pdos));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_1, 7, pdos));
	zassert_ok(memcmp(pdos, spr_pdos, sizeof(uint32_t) * 7));

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_snk_pdos(PDO_OFFSET_1, 7, spr_pdos));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_1, 7, pdos));
	zassert_ok(memcmp(pdos, spr_pdos, sizeof(uint32_t) * 7));

	/* Test mixtures of PDOS. */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, 6, mixed_pdos_success));
	zassert_ok(emul_get_src_pdos(PDO_OFFSET_1, 6, pdos));
	zassert_ok(
		memcmp(pdos, mixed_pdos_success, sizeof(mixed_pdos_success)));

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_snk_pdos(PDO_OFFSET_1, 6, mixed_pdos_success));
	zassert_ok(emul_get_snk_pdos(PDO_OFFSET_1, 6, pdos));
	zassert_ok(
		memcmp(pdos, mixed_pdos_success, sizeof(mixed_pdos_success)));

	zassert_not_ok(emul_set_src_pdos(PDO_OFFSET_1, 6, mixed_pdos_failure));
	zassert_not_ok(emul_set_snk_pdos(PDO_OFFSET_1, 6, mixed_pdos_failure));
}

ZTEST_USER(rts54xx, test_pdos)
{
	uint32_t pdos[PDO_OFFSET_MAX];

	memset(pdos, 0, sizeof(pdos));
	zassert_ok(emul_set_src_pdos(PDO_OFFSET_1, 6, mixed_pdos_success));

	/*
	 * This is implemented using the same underlying code as
	 * emul_pdc_get_pdos so we only need to do a basic test.
	 */
	memset(pdos, 0, sizeof(pdos));
	zassert_ok(pdc_get_pdos(dev, SOURCE_PDO, PDO_OFFSET_1, 6, false, pdos));
	k_sleep(K_MSEC(1000));
	zassert_ok(
		memcmp(pdos, mixed_pdos_success, sizeof(mixed_pdos_success)));
}

ZTEST_USER(rts54xx, test_get_bus_info)
{
	struct pdc_bus_info_t info;
	struct i2c_dt_spec i2c_spec = I2C_DT_SPEC_GET(RTS5453P_NODE);

	zassert_not_ok(pdc_get_bus_info(dev, NULL));

	zassert_ok(pdc_get_bus_info(dev, &info));
	zassert_equal(info.bus_type, PDC_BUS_TYPE_I2C);
	zassert_equal(info.i2c.bus, i2c_spec.bus);
	zassert_equal(info.i2c.addr, i2c_spec.addr);
}
