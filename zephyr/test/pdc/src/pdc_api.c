/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
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

void pdc_before_test(void *data)
{
	emul_pdc_set_response_delay(emul, 0);
}

ZTEST_SUITE(pdc_api, NULL, NULL, pdc_before_test, NULL, NULL);

ZTEST_USER(pdc_api, test_get_ucsi_version)
{
	uint16_t version = 0;

	zassert_not_ok(pdc_get_ucsi_version(dev, NULL));

	zassert_ok(pdc_get_ucsi_version(dev, &version));
	zassert_equal(version, UCSI_VERSION);
}

ZTEST_USER(pdc_api, test_reset)
{
	zassert_ok(pdc_reset(dev), "Failed to reset PDC");

	k_sleep(K_MSEC(500));
}

ZTEST_USER(pdc_api, test_connector_reset)
{
	enum connector_reset_t type = 0;

	emul_pdc_set_response_delay(emul, 50);
	zassert_ok(pdc_connector_reset(dev, PD_HARD_RESET),
		   "Failed to reset connector");

	k_sleep(K_MSEC(5));
	emul_pdc_get_connector_reset(emul, &type);
	zassert_not_equal(type, PD_HARD_RESET);

	k_sleep(K_MSEC(100));
	emul_pdc_get_connector_reset(emul, &type);
	zassert_equal(type, PD_HARD_RESET);
}

ZTEST_USER(pdc_api, test_get_capability)
{
	struct capability_t in, out;

	in.bcdBCVersion = 0x12;
	in.bcdPDVersion = 0x34;
	in.bcdUSBTypeCVersion = 0x56;

	zassert_ok(emul_pdc_set_capability(emul, &in));

	zassert_ok(pdc_get_capability(dev, &out), "Failed to get capability");

	k_sleep(K_MSEC(500));

	/* Verify versioning from emulator */
	zassert_equal(out.bcdBCVersion, in.bcdBCVersion);
	zassert_equal(out.bcdPDVersion, in.bcdPDVersion);
	zassert_equal(out.bcdUSBTypeCVersion, in.bcdUSBTypeCVersion);
}

ZTEST_USER(pdc_api, test_get_connector_capability)
{
	union connector_capability_t in, out;

	in.op_mode_rp_only = 1;
	in.op_mode_rd_only = 0;
	in.op_mode_usb2 = 1;
	zassert_ok(emul_pdc_set_connector_capability(emul, &in));

	zassert_ok(pdc_get_connector_capability(dev, &out),
		   "Failed to get connector capability");

	k_sleep(K_MSEC(100));

	/* Verify data from emulator */
	zassert_equal(out.op_mode_rp_only, in.op_mode_rp_only);
	zassert_equal(out.op_mode_rd_only, in.op_mode_rd_only);
	zassert_equal(out.op_mode_usb2, in.op_mode_usb2);
}

ZTEST_USER(pdc_api, test_get_error_status)
{
	union error_status_t in, out;

	in.unrecognized_command = 1;
	in.contract_negotiation_failed = 0;
	in.invalid_command_specific_param = 1;
	zassert_ok(emul_pdc_set_error_status(emul, &in));

	zassert_ok(pdc_get_error_status(dev, &out),
		   "Failed to get connector capability");
	/* TODO(b/319730714) - back 2 back calls should provide EBUSY error but
	 * driver thread doesn't become active to move out of IDLE state.
	 * zassert_equal(pdc_get_error_status(dev, &out), -EBUSY);
	 */
	k_sleep(K_MSEC(100));

	/* Verify data from emulator */
	zassert_equal(out.unrecognized_command, in.unrecognized_command);
	zassert_equal(out.contract_negotiation_failed,
		      in.contract_negotiation_failed);
	zassert_equal(out.invalid_command_specific_param,
		      in.invalid_command_specific_param);

	zassert_equal(pdc_get_error_status(dev, NULL), -EINVAL);
}

ZTEST_USER(pdc_api, test_get_connector_status)
{
	struct connector_status_t in, out;

	in.conn_status_change_bits.external_supply_change = 1;
	in.conn_status_change_bits.connector_partner = 1;
	in.conn_status_change_bits.connect_change = 1;
	in.power_operation_mode = PD_OPERATION;
	in.connect_status = 1;
	in.power_direction = 0;
	in.conn_partner_flags = 1;
	in.conn_partner_type = UFP_ATTACHED;
	in.rdo = 0x01234567;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));

	zassert_ok(pdc_get_connector_status(dev, &out),
		   "Failed to get connector capability");

	k_sleep(K_MSEC(100));

	/* Verify data from emulator */
	zassert_equal(out.conn_status_change_bits.external_supply_change,
		      in.conn_status_change_bits.external_supply_change);
	zassert_equal(out.conn_status_change_bits.connector_partner,
		      in.conn_status_change_bits.connector_partner);
	zassert_equal(out.conn_status_change_bits.connect_change,
		      in.conn_status_change_bits.connect_change);
	zassert_equal(out.power_operation_mode, in.power_operation_mode);
	zassert_equal(out.connect_status, in.connect_status);
	zassert_equal(out.power_direction, in.power_direction);
	zassert_equal(out.conn_partner_flags, in.conn_partner_flags,
		      "out=0x%X != in=0x%X", out.conn_partner_flags,
		      in.conn_partner_flags);
	zassert_equal(out.conn_partner_type, in.conn_partner_type);
	zassert_equal(out.rdo, in.rdo);
}

ZTEST_USER(pdc_api, test_set_uor)
{
	union uor_t in, out;

	in.accept_dr_swap = 1;
	in.swap_to_ufp = 1;

	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");

	k_sleep(K_MSEC(100));
	zassert_ok(emul_pdc_get_uor(emul, &out));

	zassert_equal(out.raw_value, in.raw_value);
}

ZTEST_USER(pdc_api, test_set_pdr)
{
	union pdr_t in, out;

	in.accept_pr_swap = 1;
	in.swap_to_src = 1;

	zassert_ok(pdc_set_pdr(dev, in), "Failed to set pdr");

	k_sleep(K_MSEC(100));
	zassert_ok(emul_pdc_get_pdr(emul, &out));

	zassert_equal(out.raw_value, in.raw_value);
}
