/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/ppc.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_ppc, LOG_LEVEL_DBG);

#define PPC_CHIP_STUB(node_id) {},

static struct ppc_config_t ppc_chips_saved[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, PPC_CHIP_STUB) };

ZTEST(ppc_shim, test_ppc_alts_exist)
{
	/* Verify all PPC types are able to create an alternate PPC entry */
	zassert_not_null(&PPC_ALT_FROM_NODELABEL(ppc_aoz1380_alt));
	zassert_not_null(&PPC_ALT_FROM_NODELABEL(ppc_nx20p348x_alt));
	zassert_not_null(&PPC_ALT_FROM_NODELABEL(ppc_rt1739_alt));
	zassert_not_null(&PPC_ALT_FROM_NODELABEL(ppc_syv682x_alt));
	zassert_not_null(&PPC_ALT_FROM_NODELABEL(ppc_sn5s330_alt));
}

ZTEST(ppc_shim, test_ppc_alt_enable)
{
	/* Enable an alternate PPC on each USB-C port */
	PPC_ENABLE_ALTERNATE_BY_NODELABEL(0, ppc_syv682x_alt);
	PPC_ENABLE_ALTERNATE_BY_NODELABEL(1, ppc_rt1739_alt);

	zassert_mem_equal(&ppc_chips[0],
			  &PPC_ALT_FROM_NODELABEL(ppc_syv682x_alt),
			  sizeof(struct ppc_config_t));
	zassert_mem_equal(&ppc_chips[1],
			  &PPC_ALT_FROM_NODELABEL(ppc_rt1739_alt),
			  sizeof(struct ppc_config_t));

	PPC_ENABLE_ALTERNATE_BY_NODELABEL(0, ppc_nx20p348x_alt);
	zassert_mem_equal(&ppc_chips[0],
			  &PPC_ALT_FROM_NODELABEL(ppc_nx20p348x_alt),
			  sizeof(struct ppc_config_t));
}

void ppc_shim_before_test(void *data)
{
	memcpy(ppc_chips_saved, ppc_chips,
	       sizeof(struct ppc_config_t) * ARRAY_SIZE(ppc_chips_saved));
}

void ppc_shim_after_test(void *data)
{
	memcpy(ppc_chips, ppc_chips_saved,
	       sizeof(struct ppc_config_t) * ARRAY_SIZE(ppc_chips_saved));
}

ZTEST_SUITE(ppc_shim, NULL, NULL, ppc_shim_before_test, ppc_shim_after_test,
	    NULL);
