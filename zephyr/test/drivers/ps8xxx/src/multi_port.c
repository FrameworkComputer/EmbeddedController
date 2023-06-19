/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpci.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "tcpm/tcpm.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#define PS8XXX_NODE_0 DT_NODELABEL(ps8xxx_emul0)
#define PS8XXX_NODE_1 DT_NODELABEL(ps8xxx_emul1)

ZTEST_SUITE(multi_port, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

const struct emul *ps8xxx_emul_0 = EMUL_DT_GET(PS8XXX_NODE_0);
const struct emul *ps8xxx_emul_1 = EMUL_DT_GET(PS8XXX_NODE_1);

ZTEST(multi_port, test_multiple_ports)
{
	zassert_ok(tcpci_emul_set_reg(ps8xxx_emul_0, TCPC_REG_BCD_DEV, 2),
		   "Unable to set device id for emulator 0.\n");
	zassert_ok(tcpci_emul_set_reg(ps8xxx_emul_1, TCPC_REG_BCD_DEV, 3),
		   "Unable to set device id for emulator 1.\n");

	struct ec_response_pd_chip_info_v1 info[USBC_PORT_COUNT];

	for (enum usbc_port p = USBC_PORT_C0; p < USBC_PORT_COUNT; p++) {
		zassert_ok(tcpm_get_chip_info(p, true, &info[p]),
			   "Failed to process tcpm_get_chip_info for port %d",
			   p);
	}

	zassert_true(info[USBC_PORT_C0].device_id !=
			     info[USBC_PORT_C1].device_id,
		     "port 0 and port 1 contains duplicate information.\n");
}

ZTEST(multi_port, test_fw_version_cache)
{
	zassert_ok(tcpci_emul_set_reg(ps8xxx_emul_0, PS8XXX_REG_FW_REV, 0x12),
		   "Unable to set firmware rev for emulator 0.\n");
	zassert_ok(tcpci_emul_set_reg(ps8xxx_emul_1, PS8XXX_REG_FW_REV, 0x13),
		   "Unable to set firmware rev for emulator 1.\n");

	struct ec_response_pd_chip_info_v1 info[USBC_PORT_COUNT];

	for (enum usbc_port p = USBC_PORT_C0; p < USBC_PORT_COUNT; p++) {
		zassert_ok(tcpm_get_chip_info(p, true, &info[p]),
			   "Failed to process tcpm_get_chip_info for port %d",
			   p);
	}

	/* info is read from the cache the second time */
	for (enum usbc_port p = USBC_PORT_C0; p < USBC_PORT_COUNT; p++) {
		zassert_ok(tcpm_get_chip_info(p, false, &info[p]),
			   "Failed to process tcpm_get_chip_info for port %d",
			   p);
	}

	zassert_true(info[USBC_PORT_C0].fw_version_number == 0x12,
		     "port 0 fw version cache is incorrect.\n");
	zassert_true(info[USBC_PORT_C1].fw_version_number == 0x13,
		     "port 1 fw version cache is incorrect.\n");
}
