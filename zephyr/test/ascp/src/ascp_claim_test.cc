/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/ztest.h>

#include <algorithm>
#include <array>
#include <ascp/ascp.h>
#include <ranges>

static constexpr std::array<uint8_t, 65> pk_m = { 'p', 'k', 'm' };
static constexpr std::array<uint8_t, 64> s_goog = { 's', 'g', 'o', 'o', 'g' };
static constexpr std::array<uint8_t, 65> pk_d = { 'p', 'k', 'd' };
static constexpr std::array<uint8_t, 64> s_m = { 's', 'm' };
static constexpr std::array<uint8_t, 65> pk_f = { 'p', 'k', 'f' };
static constexpr std::array<uint8_t, 32> h_f = { 'h', 'f' };
static constexpr std::array<uint8_t, 64> s_d = { 's', 'd' };

static int ascp_get_claim_custom_ret;

extern "C" int ascp_get_claim(ec_response_fp_ascp_claim *res)
{
	std::ranges::copy(pk_m, std::begin(res->pk_m));
	std::ranges::copy(s_goog, std::begin(res->s_goog));
	std::ranges::copy(pk_d, std::begin(res->pk_d));
	std::ranges::copy(s_m, std::begin(res->s_m));
	std::ranges::copy(pk_f, std::begin(res->pk_f));
	std::ranges::copy(h_f, std::begin(res->h_f));
	std::ranges::copy(s_d, std::begin(res->s_d));
	return ascp_get_claim_custom_ret;
}

ZTEST(ascp_claim, test_ok)
{
	int ret;
	ec_response_fp_ascp_claim res;

	ret = ec_cmd_fp_ascp_claim(nullptr, &res);
	zassert_true(std::ranges::equal(res.pk_m, pk_m));
	zassert_true(std::ranges::equal(res.pk_d, pk_d));
	zassert_true(std::ranges::equal(res.pk_f, pk_f));
	zassert_true(std::ranges::equal(res.s_goog, s_goog));
	zassert_true(std::ranges::equal(res.s_m, s_m));
	zassert_true(std::ranges::equal(res.s_d, s_d));
	zassert_true(std::ranges::equal(res.h_f, h_f));
	zassert_equal(EC_RES_SUCCESS, ret);
}

ZTEST(ascp_claim, test_custom_not_ok)
{
	int ret;
	ec_response_fp_ascp_claim res;

	ascp_get_claim_custom_ret = -1;
	ret = ec_cmd_fp_ascp_claim(nullptr, &res);
	zassert_equal(EC_RES_ERROR, ret);
}

ZTEST(ascp_claim, test_args_not_ok)
{
	int ret;
	int too_small_res;

	ret = CROS_EC_COMMAND(nullptr, EC_CMD_FP_ASCP_CLAIM, 0, nullptr, 0,
			      &too_small_res, sizeof(too_small_res));
	zassert_equal(EC_RES_RESPONSE_TOO_BIG, ret);
}

ZTEST(ascp_claim, test_shell_command_success)
{
	char console_input[] = "fpascp";
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_SUCCESS);
}

ZTEST(ascp_claim, test_shell_command_failure)
{
	char console_input[] = "fpascp";
	ascp_get_claim_custom_ret = EC_RES_ERROR;
	int rv = shell_execute_cmd(get_ec_shell(), console_input);
	zassert_equal(rv, EC_RES_ERROR);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	ascp_get_claim_custom_ret = 0;
}

ZTEST_SUITE(ascp_claim, nullptr, nullptr, reset, reset, nullptr);
