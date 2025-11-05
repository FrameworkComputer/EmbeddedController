/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src_policy_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

LOG_MODULE_REGISTER(pdc_src_policy_common);

struct ec_response_usb_pd_power_info host_cmd_power_info(int port)
{
	struct ec_params_usb_pd_power_info params = { .port = port };
	struct ec_response_usb_pd_power_info response;

	zassert_ok(ec_cmd_usb_pd_power_info(NULL, &params, &response),
		   "Failed to get power info for port %d", port);
	return response;
}

int verify_lpm_source_pdo(struct src_policy_fixture *fixture, uint32_t port,
			  int mv, int ma, int PDO_PEAK_OCP)
{
	uint32_t lpm_src_pdo;

	emul_pdc_get_pdos(fixture->emul_pdc[port], SOURCE_PDO, PDO_OFFSET_0, 1,
			  LPM_PDO, &lpm_src_pdo);

	if (PDO_FIXED_VOLTAGE(lpm_src_pdo) != mv) {
		/* LCOV_EXCL_START - error path only run when test fails */
		LOG_ERR("Expected fixed voltage %d mV, actual %d mV", mv,
			PDO_FIXED_VOLTAGE(lpm_src_pdo));
		return -ERANGE;
		/* LCOV_EXCL_STOP */
	}

	if (PDO_FIXED_CURRENT(lpm_src_pdo) != ma) {
		/* LCOV_EXCL_START - error path only run when test fails */
		LOG_ERR("Expected fixed current %d mA, actual %d mA", ma,
			PDO_FIXED_CURRENT(lpm_src_pdo));
		return -ERANGE;
		/* LCOV_EXCL_STOP */
	}

	if (PDO_FIXED_GET_PEAK_CURR(lpm_src_pdo) != PDO_PEAK_OCP) {
		/* LCOV_EXCL_START - error path only run when test fails */
		LOG_ERR("Expected peak current %d, actual %d", PDO_PEAK_OCP,
			PDO_FIXED_GET_PEAK_CURR(lpm_src_pdo));
		return -ERANGE;
		/* LCOV_EXCL_STOP */
	}
	return 0;
}
