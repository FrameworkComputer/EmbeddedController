/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "mock_usbc_power_contract.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"

#include <stdint.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

struct usbc_power_contract_fixture {
	struct tcpci_partner_data sink;
	struct tcpci_faulty_ext_data faulty_snk_ext;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_faulty_ext_action actions[2];
	enum usbc_port port;
};

static void *usbc_power_contract_setup(void)
{
	static struct usbc_power_contract_fixture test_fixture;

	test_fixture.port = TEST_PORT;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	/* Initialized the sink to request 5V and 3A */
	tcpci_partner_init(&test_fixture.sink, PD_REV20);
	test_fixture.sink.extensions = tcpci_faulty_ext_init(
		&test_fixture.faulty_snk_ext, &test_fixture.sink,
		tcpci_snk_emul_init(&test_fixture.snk_ext, &test_fixture.sink,
				    NULL));

	return &test_fixture;
}

static void usbc_power_contract_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void usbc_power_contract_after(void *data)
{
	struct usbc_power_contract_fixture *fixture = data;

	tcpci_faulty_ext_clear_actions_list(&fixture->faulty_snk_ext);
	disconnect_sink_from_port(fixture->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fixture->sink);
}

ZTEST_SUITE(usbc_power_contract, drivers_predicate_post_main,
	    usbc_power_contract_setup, usbc_power_contract_before,
	    usbc_power_contract_after, NULL);

/**
 * @brief Test that Reject message is sent when source has no PDOs
 */
ZTEST_F(usbc_power_contract, test_reject_msg_sent_when_no_pdos)
{
	struct tcpci_partner_log_msg *msg;
	uint16_t header;
	int saw_reject = 0;

	/*
	 * dpm_get_source_pdo() returns 0 to indicate no source PDOs
	 * are available to send
	 */
	dpm_get_source_pdo_fake.return_val = 0;

	tcpci_partner_common_enable_pd_logging(&fixture->sink, true);
	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);
	tcpci_partner_send_control_msg(&fixture->sink, PD_CTRL_GET_SOURCE_CAP,
				       0);
	k_sleep(K_SECONDS(2));
	tcpci_partner_common_enable_pd_logging(&fixture->sink, false);

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->sink.msg_log, msg, node)
	{
		header = sys_get_le16(msg->buf);
		if (PD_HEADER_TYPE(header) == PD_CTRL_REJECT) {
			saw_reject++;
		}
	}

	zassert_true(saw_reject > 0, "Reject message was not sent");
}
