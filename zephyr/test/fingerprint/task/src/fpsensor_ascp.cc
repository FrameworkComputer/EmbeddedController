/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "fpsensor/fpsensor_auth_crypto.h"
#include "openssl/mem.h"
#include "system.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <algorithm>
extern "C" {
#include <ascp/ascp.h>
}
#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor/fpsensor_utils.h>
#include <fpsensor_driver.h>
#include <host_command.h>
#include <mkbp_event.h>
#include <ranges>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

extern std::array<uint8_t, FP_PAIRING_KEY_LEN> pairing_key;

static constexpr std::array<uint8_t, 32> sk_f = {
	0x64, 0xfb, 0xd5, 0x8f, 0x6b, 0x57, 0x2a, 0x41, 0xef, 0x00, 0x0e,
	0x18, 0x92, 0xf8, 0xb5, 0x52, 0xc7, 0x41, 0x34, 0x35, 0xb1, 0xce,
	0x7c, 0x11, 0xfc, 0xf5, 0xa5, 0x94, 0x97, 0xaa, 0xc4, 0x91
};

static constexpr std::array<uint8_t, 65> pk_f = {
	0x04, 0x48, 0x04, 0x5c, 0x21, 0x5b, 0x8d, 0x9b, 0x23, 0x31, 0x95,
	0x0e, 0x2a, 0x8d, 0x8f, 0x26, 0x22, 0x66, 0xe9, 0x59, 0xc2, 0x37,
	0xc9, 0x4f, 0x94, 0x73, 0x27, 0xc2, 0xb1, 0x11, 0xa0, 0x01, 0x78,
	0x13, 0x2f, 0x08, 0x9e, 0x94, 0xe7, 0x6d, 0x58, 0x76, 0xa1, 0x1a,
	0x20, 0xda, 0x0c, 0xc1, 0xef, 0x91, 0xaf, 0xb1, 0x5d, 0x48, 0x81,
	0x1b, 0x6b, 0xe1, 0x6e, 0xc0, 0xd5, 0xfb, 0x9d, 0x69, 0x82
};

static constexpr std::array<uint8_t, 32> sk_g = {
	0x2e, 0xdd, 0x7a, 0x67, 0x2f, 0x22, 0x21, 0x5c, 0x23, 0x80, 0xf0,
	0x80, 0x29, 0x5c, 0x6d, 0x95, 0xe9, 0x0b, 0x31, 0x9c, 0x17, 0xc1,
	0xc6, 0xcc, 0xe3, 0xbe, 0x9b, 0x7c, 0xd7, 0x2a, 0x18, 0x3f
};

static constexpr std::array<uint8_t, 65> pk_g = {
	0x04, 0x65, 0xf5, 0xd4, 0x79, 0x82, 0x7e, 0xc1, 0xaf, 0x28, 0x2e,
	0x4b, 0xd0, 0xa6, 0xcb, 0xf4, 0xf4, 0xf5, 0x16, 0x63, 0xa6, 0x85,
	0x40, 0xdc, 0xa4, 0x26, 0xe8, 0x63, 0xc3, 0xa8, 0x52, 0xd3, 0x25,
	0x42, 0x8d, 0xd4, 0x7d, 0x85, 0x30, 0x6c, 0x16, 0x6e, 0x99, 0x24,
	0x65, 0x5c, 0x6d, 0x9b, 0x04, 0xfe, 0xfb, 0x83, 0x49, 0x9d, 0xa9,
	0x2a, 0xae, 0x75, 0x3e, 0x79, 0xdc, 0xa1, 0x80, 0x6e, 0x6e
};

static std::array<uint8_t, 32> ascp_get_sk_f_custom_sk_f;
static int ascp_get_sk_f_custom_ret;

extern "C" int ascp_get_claim(ec_response_fp_ascp_claim *res)
{
	return EC_SUCCESS;
}

extern "C" int ascp_get_sk_f(uint8_t *buffer, size_t len)
{
	zassert_equal(len, FP_ELLIPTIC_CURVE_PRIVATE_KEY_LEN);
	if (ascp_get_sk_f_custom_ret != 0)
		return ascp_get_sk_f_custom_ret;
	std::ranges::copy(ascp_get_sk_f_custom_sk_f, buffer);
	return EC_SUCCESS;
}

ZTEST(hc_fp_ascp, test_fp_ascp_establish_ok)
{
	int ret;
	ec_params_fp_ascp_establish params;
	std::ranges::copy(pk_g, std::begin(params.pk_g));

	ret = ec_cmd_fp_ascp_establish(nullptr, &params);
	zassert_equal(EC_RES_SUCCESS, ret);

	bssl::UniquePtr<EC_KEY> public_key = create_ec_key_from_pubkey(
		*reinterpret_cast<const fp_elliptic_curve_public_key *>(
			&pk_f[1]));
	bssl::UniquePtr<EC_KEY> private_key =
		create_ec_key_from_privkey(sk_g.data(), sk_g.size());
	zassert_not_equal(public_key, nullptr);
	zassert_not_equal(private_key, nullptr);

	std::array<uint8_t, FP_PAIRING_KEY_LEN> host_pairing_key;
	ret = generate_ecdh_shared_secret_without_kdf(*private_key, *public_key,
						      host_pairing_key);
	zassert_equal(EC_SUCCESS, ret);

	zassert_true(std::ranges::equal(host_pairing_key, pairing_key));
}

ZTEST(hc_fp_ascp, test_fp_ascp_establish_pk_g_first_not_ok)
{
	int ret;
	ec_params_fp_ascp_establish params;
	std::ranges::copy(pk_g, std::begin(params.pk_g));

	params.pk_g[0] = 0x05;

	ret = ec_cmd_fp_ascp_establish(nullptr, &params);
	zassert_equal(EC_RES_INVALID_PARAM, ret);
}

ZTEST(hc_fp_ascp, test_fp_ascp_establish_pk_g_later_not_ok)
{
	int ret;
	ec_params_fp_ascp_establish params;
	std::ranges::copy(pk_g, std::begin(params.pk_g));

	params.pk_g[1] = 0x00;

	ret = ec_cmd_fp_ascp_establish(nullptr, &params);
	zassert_equal(EC_RES_INVALID_PARAM, ret);
}

ZTEST(hc_fp_ascp, test_fp_ascp_establish_ascp_get_sk_f_not_ok)
{
	int ret;
	ec_params_fp_ascp_establish params;
	std::ranges::copy(pk_g, std::begin(params.pk_g));

	ascp_get_sk_f_custom_ret = -1;

	ret = ec_cmd_fp_ascp_establish(nullptr, &params);
	zassert_equal(EC_RES_ERROR, ret);
}

ZTEST(hc_fp_ascp, test_fp_ascp_establish_sk_f_not_ok)
{
	int ret;
	ec_params_fp_ascp_establish params;
	std::ranges::copy(pk_g, std::begin(params.pk_g));

	std::ranges::fill(ascp_get_sk_f_custom_sk_f, 0);

	ret = ec_cmd_fp_ascp_establish(nullptr, &params);
	zassert_equal(EC_RES_ERROR, ret);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	ascp_get_sk_f_custom_ret = 0;
	std::ranges::copy(sk_f, std::begin(ascp_get_sk_f_custom_sk_f));
}

ZTEST_SUITE(hc_fp_ascp, nullptr, nullptr, reset, reset, nullptr);
