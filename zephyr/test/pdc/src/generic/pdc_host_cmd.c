/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "drivers/ucsi_v3.h"
#include "ec_commands.h"
#include "emul/emul_pdc.h"
#include "mock_pdc_power_mgmt.h"

#include <zephyr/ztest.h>

#define TEST_PORT 0

BUILD_ASSERT(TEST_PORT < CONFIG_USB_PD_PORT_MAX_COUNT);

static void host_cmd_pdc_reset(void *fixture)
{
	helper_reset_pdc_power_mgmt_fakes();
}

ZTEST_SUITE(host_cmd_pdc, NULL, NULL, host_cmd_pdc_reset, host_cmd_pdc_reset,
	    NULL);

const static struct pdc_info_t info = {
	/* 10.20.30 */
	.fw_version = (10 << 16) | (20 << 8) | (30 << 0),
	.pd_revision = 123,
	.pd_version = 456,
	/* VID:PID = 7890:3456 */
	.vid_pid = (0x7890 << 16) | (0x3456 << 0),
	.is_running_flash_code = 1,
	.running_in_flash_bank = 16,
	.project_name = "ProjectName",
	.extra = 0xffff,
	.driver_name = "driver_name",
	.no_fw_update = true,
};

/**
 * @brief Custom fake for pdc_power_mgmt_get_info that outputs some test PDC
 *        chip info.
 */
static int custom_fake_pdc_power_mgmt_get_info(int port, struct pdc_info_t *out,
					       bool live)
{
	zassert_not_null(out);

	*out = info;

	return 0;
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v0)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info resp;

	/* Error calling pdc_power_mgmt_chip_info() */
	pdc_power_mgmt_get_info_fake.return_val = -1;

	rv = ec_cmd_pd_chip_info(NULL, &req, &resp);

	zassert_equal(EC_RES_ERROR, rv, "Got %d, expected %d", rv,
		      EC_RES_ERROR);

	RESET_FAKE(pdc_power_mgmt_get_info);

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(PDC_VIDPID_GET_VID(info.vid_pid), resp.vendor_id);
	zassert_equal(PDC_VIDPID_GET_PID(info.vid_pid), resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v1)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v1 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v1(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(PDC_VIDPID_GET_VID(info.vid_pid), resp.vendor_id);
	zassert_equal(PDC_VIDPID_GET_PID(info.vid_pid), resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v2)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v2 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v2(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(PDC_VIDPID_GET_VID(info.vid_pid), resp.vendor_id);
	zassert_equal(PDC_VIDPID_GET_PID(info.vid_pid), resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);

	/* Fields added in V2 */
	zassert_mem_equal(info.project_name, resp.fw_name_str,
			  sizeof(info.project_name));
	zassert_equal(info.no_fw_update,
		      !!(resp.fw_update_flags &
			 USB_PD_CHIP_INFO_FWUP_FLAG_NO_UPDATE));
}

ZTEST(host_cmd_pdc, test_ec_cmd_pd_chip_info_v3)
{
	int rv;
	struct ec_params_pd_chip_info req = {
		.port = 0,
		.live = false,
	};
	struct ec_response_pd_chip_info_v3 resp;

	/* Successful path */
	pdc_power_mgmt_get_info_fake.custom_fake =
		custom_fake_pdc_power_mgmt_get_info;

	rv = ec_cmd_pd_chip_info_v3(NULL, &req, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);

	zassert_equal(PDC_VIDPID_GET_VID(info.vid_pid), resp.vendor_id);
	zassert_equal(PDC_VIDPID_GET_PID(info.vid_pid), resp.product_id);
	zassert_equal(PDC_FWVER_GET_MAJOR(info.fw_version),
		      resp.fw_version_string[2]);
	zassert_equal(PDC_FWVER_GET_MINOR(info.fw_version),
		      resp.fw_version_string[1]);
	zassert_equal(PDC_FWVER_GET_PATCH(info.fw_version),
		      resp.fw_version_string[0]);

	/* Field added in V1, but not used by the PDC code */
	zassert_equal(0, resp.min_req_fw_version_number);

	/* Fields added in V2 */
	zassert_mem_equal(info.project_name, resp.fw_name_str,
			  sizeof(info.project_name));
	zassert_equal(info.no_fw_update,
		      !!(resp.fw_update_flags &
			 USB_PD_CHIP_INFO_FWUP_FLAG_NO_UPDATE));

	/* Field added in V3 */
	zassert_ok(strncmp(info.driver_name, resp.driver_name,
			   strlen(info.driver_name)));
}

ZTEST(host_cmd_pdc, test_ec_cmd_usb_pd_ports)
{
	int rv;
	struct ec_response_usb_pd_ports resp;

	rv = ec_cmd_usb_pd_ports(NULL, &resp);

	zassert_equal(EC_RES_SUCCESS, rv, "Got %d, expected %d", rv,
		      EC_RES_SUCCESS);
	zassert_equal(CONFIG_USB_PD_PORT_MAX_COUNT, resp.num_ports);
}
