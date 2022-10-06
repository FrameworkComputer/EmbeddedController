/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "emul/tcpc/emul_tcpci.h"
#include "motion_sense_fifo.h"
#include "test/drivers/stubs.h"
#include "test/drivers/utils.h"
#include "usb_pd_tcpm.h"

static void motion_sense_fifo_reset_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	motion_sense_fifo_reset();
}
ZTEST_RULE(motion_sense_fifo_reset, motion_sense_fifo_reset_before, NULL);

static void tcpci_revision_reset_before(const struct ztest_unit_test *test,
					void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);
	const struct emul *tcpc_c0_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	const struct emul *tcpc_c1_emul = EMUL_GET_USBC_BINDING(1, tcpc);

	/* Set TCPCI to revision 2 for both emulators */
	tcpc_config[USBC_PORT_C0].flags |= TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(tcpc_c0_emul, TCPCI_EMUL_REV2_0_VER1_1);

	tcpc_config[USBC_PORT_C1].flags |= TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(tcpc_c1_emul, TCPCI_EMUL_REV2_0_VER1_1);
}
ZTEST_RULE(tcpci_revision_reset, tcpci_revision_reset_before, NULL);
