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
#include "i2c.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_pdc_api, LOG_LEVEL_INF);

#define RTS5453P_NODE DT_NODELABEL(rts5453p_emul)

static const struct emul *emul = EMUL_DT_GET(RTS5453P_NODE);
static const struct device *dev = DEVICE_DT_GET(RTS5453P_NODE);

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

ZTEST_USER(pdc_api, test_rdo)
{
	uint32_t in, out = 0;

	in = BIT(25) | (BIT_MASK(9) & 0x55);
	zassert_ok(pdc_set_rdo(dev, in));

	k_sleep(K_MSEC(100));
	zassert_ok(pdc_get_rdo(dev, &out));

	k_sleep(K_MSEC(100));
	zassert_equal(in, out);
}

ZTEST_USER(pdc_api, test_set_power_level)
{
	int i;
	enum usb_typec_current_t out;
	enum usb_typec_current_t in[] = {
		TC_CURRENT_USB_DEFAULT,
		TC_CURRENT_1_5A,
		TC_CURRENT_3_0A,
	};

	zassert_equal(pdc_set_power_level(dev, TC_CURRENT_PPM_DEFINED),
		      -EINVAL);

	for (i = 0; i < ARRAY_SIZE(in); i++) {
		zassert_ok(pdc_set_power_level(dev, in[i]));

		k_sleep(K_MSEC(100));
		emul_pdc_get_requested_power_level(emul, &out);
		zassert_equal(in[i], out);
	}
}

ZTEST_USER(pdc_api, test_get_bus_voltage)
{
	uint32_t mv_units = 50;
	uint32_t expected_voltage_mv = 5000;
	uint16_t out = 0;
	struct connector_status_t in;

	in.voltage_scale = 10; /* 50 mv units*/
	in.voltage_reading = expected_voltage_mv / mv_units;
	emul_pdc_set_connector_status(emul, &in);

	zassert_ok(pdc_get_vbus_voltage(dev, &out));
	k_sleep(K_MSEC(100));

	zassert_equal(out, expected_voltage_mv);

	zassert_equal(pdc_get_vbus_voltage(dev, NULL), -EINVAL);
}

ZTEST_USER(pdc_api, test_set_ccom)
{
	int i, j;
	enum ccom_t ccom_in[] = { CCOM_RP, CCOM_RD, CCOM_DRP };
	enum ccom_t ccom_out;
	enum drp_mode_t dm_in[] = { DRP_NORMAL, DRP_TRY_SRC, DRP_TRY_SNK };
	enum drp_mode_t dm_out;

	for (i = 0; i < ARRAY_SIZE(ccom_in); i++) {
		for (j = 0; j < ARRAY_SIZE(dm_in); j++) {
			zassert_ok(pdc_set_ccom(dev, ccom_in[i], dm_in[j]));

			k_sleep(K_MSEC(100));
			zassert_ok(emul_pdc_get_ccom(emul, &ccom_out, &dm_out));
			zassert_equal(ccom_in[i], ccom_out);
			if (ccom_in[i] == CCOM_DRP) {
				zassert_equal(dm_in[j], dm_out);
			}
		}
	}
}

ZTEST_USER(pdc_api, test_set_sink_path)
{
	int i;
	bool in[] = { true, false }, out;

	for (i = 0; i < ARRAY_SIZE(in); i++) {
		zassert_ok(pdc_set_sink_path(dev, in[i]));

		k_sleep(K_MSEC(100));
		zassert_ok(emul_pdc_get_sink_path(emul, &out));

		zassert_equal(in[i], out);
	}
}

ZTEST_USER(pdc_api, test_reconnect)
{
	uint8_t expected, val;

	zassert_ok(pdc_reconnect(dev));

	k_sleep(K_MSEC(100));
	zassert_ok(emul_pdc_get_reconnect_req(emul, &expected, &val));
	zassert_equal(expected, val);
}

ZTEST_USER(pdc_api, test_get_info)
{
	struct pdc_info_t in, out;

	zassert_equal(-EINVAL, pdc_get_info(dev, NULL));

	in.fw_version = 0x010203;
	in.pd_version = 0x0506;
	in.pd_revision = 0x0708;
	in.vid_pid = 0xFEEDBEEF;

	emul_pdc_set_info(emul, &in);
	zassert_ok(pdc_get_info(dev, &out));
	k_sleep(K_MSEC(100));

	zassert_equal(in.fw_version, out.fw_version, "in=0x%X, out=0x%X",
		      in.fw_version, out.fw_version);
	zassert_equal(in.pd_version, out.pd_version);
	zassert_equal(in.pd_revision, out.pd_revision);
	zassert_equal(in.vid_pid, out.vid_pid, "in=0x%X, out=0x%X", in.vid_pid,
		      out.vid_pid);
}
