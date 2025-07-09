/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "emul/tcpc/emul_raa489000.h"
#include "emul/tcpc/emul_tcpci.h"
#include "test/drivers/tcpci_test_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define RAA489000_PORT 0
#define RAA489000_EMUL_NODE DT_NODELABEL(raa489000_emul)

FAKE_VALUE_FUNC(bool, pd_vbus_valid_for_bist, int);

ZTEST(tcpc_raa489000, test_check_vendor)
{
	int v;

	zassert_ok(tcpc_read16(RAA489000_PORT, TCPC_REG_VENDOR_ID, &v));
	zassert_equal(v, 0x45b);

	tcpm_dump_registers(RAA489000_PORT);
}

void raa489000_get_rx_message_raw(const struct emul *emul,
				  struct i2c_common_emul_data *common_data,
				  enum usbc_port port)
{
	const struct tcpm_drv *drv = tcpc_config[port].drv;
	struct tcpci_emul_msg msg;
	uint32_t payload[1];
	/* bist mode message header and payload
	 *
	 * TODO(b/359666332): A BIST Test Data message should have 7 data
	 * objects, although this should not affect the results of this test.
	 */
	uint16_t input_header = PD_HEADER(PD_DATA_BIST, PD_ROLE_SINK,
					  PD_ROLE_UFP, /*msg_id=*/0, /*cnt=*/1,
					  /*rev=*/PD_REV30,
					  /*ext=*/false);
	uint32_t input_bdo = BDO(BDO_MODE_TEST_DATA, 0);
	uint8_t buf[sizeof(input_header) + sizeof(input_bdo)];

	*(uint16_t *)buf = input_header;
	*(uint32_t *)(buf + sizeof(input_header)) = input_bdo;

	int head;
	bool bist_enabled;

	RESET_FAKE(pd_vbus_valid_for_bist);
	pd_vbus_valid_for_bist_fake.return_val = true;

	tcpci_emul_set_reg(emul, TCPC_REG_RX_DETECT,
			   TCPC_REG_RX_DETECT_SOP | TCPC_REG_RX_DETECT_SOPP);

	msg.buf = buf;
	msg.cnt = 31;
	msg.sop_type = TCPCI_MSG_SOP;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");

	/* Test fail on reading message */
	i2c_common_emul_set_read_fail_reg(common_data, TCPC_REG_RX_BUFFER);
	zassert_equal(EC_ERROR_UNKNOWN,
		      drv->get_message_raw(port, payload, &head), NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test bist mode true */
	msg.buf = buf;
	msg.cnt = sizeof(buf);
	msg.sop_type = TCPCI_MSG_SOP;
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");

	zassert_equal(EC_SUCCESS, drv->set_bist_test_mode(port, false));
	zassert_equal(EC_SUCCESS, drv->get_message_raw(port, payload, &head),
		      NULL);
	zassert_equal(EC_SUCCESS, drv->get_bist_test_mode(port, &bist_enabled),
		      NULL);
	zassert_true(bist_enabled, "BIST mode should be enabled");

	/* Test bist mode already true */
	zassert_equal(TCPCI_EMUL_TX_SUCCESS,
		      tcpci_emul_add_rx_msg(emul, &msg, true),
		      "Failed to setup emulator message");

	zassert_equal(EC_SUCCESS, drv->set_bist_test_mode(port, true));
	zassert_equal(EC_SUCCESS, drv->get_message_raw(port, payload, &head),
		      NULL);
	zassert_equal(EC_SUCCESS, drv->get_bist_test_mode(port, &bist_enabled),
		      NULL);
	zassert_true(bist_enabled, "BIST mode should be enabled");
}

ZTEST(tcpc_raa489000, test_raa489000_get_rx_message_raw)
{
	const struct emul *raa489000_emul = EMUL_DT_GET(RAA489000_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(raa489000_emul);

	/* set battery level to avoid entering low battery status */
	test_set_battery_level(75);

	raa489000_get_rx_message_raw(raa489000_emul, common_data,
				     RAA489000_PORT);
}

ZTEST_SUITE(tcpc_raa489000, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
