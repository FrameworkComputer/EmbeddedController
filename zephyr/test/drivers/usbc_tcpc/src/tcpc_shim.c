/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/tcpci.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_tcpc, LOG_LEVEL_DBG);

#define TCPC_CHIP_STUB(node_id) {},

static struct tcpc_config_t tcpc_config_saved[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, TCPC_CHIP_STUB) };

ZTEST(tcpc_shim, test_tcpc_alts_exist)
{
	/* Verify all TCPC types are able to create an alternate TCPC entry */
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_anx7447_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_ccgxxf_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_fusb302_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_ps8xxx_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_raa489000_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_nct38xx_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_rt1715_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_rt1718s_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_alt));
}

ZTEST(tcpc_shim, test_tcpc_alt_enable)
{
	/* Enable an alternate TCPC on each USB-C port */
	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(0, tcpc_ps8xxx_alt);
	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(1, tcpc_rt1715_alt);

	zassert_mem_equal(&tcpc_config[0],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_ps8xxx_alt),
			  sizeof(struct tcpc_config_t));
	zassert_mem_equal(&tcpc_config[1],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_rt1715_alt),
			  sizeof(struct tcpc_config_t));

	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(0, tcpc_anx7447_alt);
	zassert_mem_equal(&tcpc_config[0],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_anx7447_alt),
			  sizeof(struct tcpc_config_t));
}

void tcpc_shim_before_test(void *data)
{
	memcpy(tcpc_config_saved, tcpc_config,
	       sizeof(struct tcpc_config_t) * ARRAY_SIZE(tcpc_config_saved));
}

void tcpc_shim_after_test(void *data)
{
	memcpy(tcpc_config, tcpc_config_saved,
	       sizeof(struct tcpc_config_t) * ARRAY_SIZE(tcpc_config_saved));
}

ZTEST_SUITE(tcpc_shim, NULL, NULL, tcpc_shim_before_test, tcpc_shim_after_test,
	    NULL);
