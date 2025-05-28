/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "panic.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

struct host_cmd_get_panic_info_fixture {
	struct panic_data saved_pdata;
};

static void *host_cmd_get_panic_info_setup(void)
{
	static struct host_cmd_get_panic_info_fixture fixture = { 0 };

	return &fixture;
}

static void host_cmd_get_panic_info_before(void *f)
{
	struct host_cmd_get_panic_info_fixture *fixture = f;
	struct panic_data *pdata = get_panic_data_write();

	fixture->saved_pdata = *pdata;
}

static void host_cmd_get_panic_info_after(void *f)
{
	struct host_cmd_get_panic_info_fixture *fixture = f;
	struct panic_data *pdata = get_panic_data_write();

	*pdata = fixture->saved_pdata;
}

ZTEST_SUITE(host_cmd_get_panic_info, drivers_predicate_post_main,
	    host_cmd_get_panic_info_setup, host_cmd_get_panic_info_before,
	    host_cmd_get_panic_info_after, NULL);

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info)
{
	struct panic_data *pdata = get_panic_data_write();
	struct panic_data response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(0), response);

	pdata->arch = 0;
	pdata->struct_version = 1;
	pdata->flags = 2;
	pdata->reserved = 3;
	pdata->struct_size = sizeof(struct panic_data);
	pdata->magic = PANIC_DATA_MAGIC;

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(sizeof(struct panic_data), args.response_size, NULL);
	zassert_equal(0, response.arch, NULL);
	zassert_equal(1, response.struct_version, NULL);
	zassert_equal(2, response.flags, NULL);
	zassert_equal(3, response.reserved, NULL);
	zassert_equal(sizeof(struct panic_data), response.struct_size, NULL);
	zassert_equal(PANIC_DATA_MAGIC, response.magic, NULL);
	zassert_equal(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD,
		      PANIC_DATA_FLAG_OLD_HOSTCMD, NULL);
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_bad_magic)
{
	struct panic_data *pdata = get_panic_data_write();
	struct panic_data expected = { 0 };
	struct panic_data response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(0), response);

	pdata->magic = PANIC_DATA_MAGIC + 1;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, 0);
	/* Check that nothing was written to response */
	zassert_mem_equal(&response, &expected, sizeof(struct panic_data),
			  NULL);
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_size_is_zero)
{
	struct panic_data *pdata = get_panic_data_write();
	struct panic_data expected = { 0 };
	struct panic_data response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(0), response);

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = 0;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, 0);
	/* Check that nothing was written to response */
	zassert_mem_equal(&response, &expected, sizeof(struct panic_data),
			  NULL);
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_truncate)
{
	struct panic_data *pdata = get_panic_data_write();
	struct panic_data response = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(0), response);

	/* Panic data is bigger than max response size, should be truncated */
	args.response_max = sizeof(struct panic_data) - 2;

	pdata->flags = 0;
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(struct panic_data);

	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, sizeof(struct panic_data) - 2);
	zassert_equal(sizeof(struct panic_data) - 2, args.response_size, NULL);
	/* Truncation flag must be set */
	zassert_equal(response.flags & PANIC_DATA_FLAG_TRUNCATED,
		      PANIC_DATA_FLAG_TRUNCATED, NULL);
	zassert_equal(sizeof(struct panic_data), response.struct_size, NULL);
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_v2_multi_part)
{
	struct panic_data *pdata = get_panic_data_write();
	uint8_t response[sizeof(struct panic_data)] = { 0 };
	struct ec_params_get_panic_info_v2 params = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(2), response);

	args.params = &params;
	args.params_size = sizeof(params);

	pdata->arch = 1;
	pdata->struct_version = 2;
	pdata->flags = 0;
	pdata->reserved = 0;
	pdata->struct_size = sizeof(struct panic_data);
	pdata->magic = PANIC_DATA_MAGIC;

	/* Read first half */
	args.response_max = sizeof(struct panic_data) / 2;
	params.read_offset = 0;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, args.response_max);
	/* Flag should NOT be set yet */
	zassert_equal(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD, 0);

	/* Read second half */
	uint32_t first_half_size = args.response_size;
	args.response = response + first_half_size;
	args.response_max = sizeof(response) - first_half_size;
	params.read_offset = first_half_size;
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, args.response_max);
	/* Flag should BE set now */
	zassert_equal(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD,
		      PANIC_DATA_FLAG_OLD_HOSTCMD);

	/*
	 * Verify data. pdata->flags was updated by the handler during the
	 * second read, so we must manually update our copy in 'response'
	 * to match it before comparing.
	 */
	((struct panic_data *)response)->flags |= PANIC_DATA_FLAG_OLD_HOSTCMD;
	zassert_mem_equal(response, pdata, sizeof(struct panic_data));
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_v2_eof)
{
	struct panic_data *pdata = get_panic_data_write();
	uint8_t response[sizeof(struct panic_data)] = { 0 };
	struct ec_params_get_panic_info_v2 params = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(2), response);

	args.params = &params;
	args.params_size = sizeof(params);

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(struct panic_data);

	/* Read at offset == size (should return 0 bytes, success) */
	args.response_size = 1; /* Non-zero to ensure it's cleared */
	params.read_offset = sizeof(struct panic_data);
	zassert_ok(host_command_process(&args), NULL);
	zassert_equal(args.response_size, 0);
}

ZTEST_USER(host_cmd_get_panic_info, test_get_panic_info_v2_invalid_offset)
{
	struct panic_data *pdata = get_panic_data_write();
	uint8_t response[sizeof(struct panic_data)] = { 0 };
	struct ec_params_get_panic_info_v2 params = { 0 };
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_PANIC_INFO, UINT8_C(2), response);

	args.params = &params;
	args.params_size = sizeof(params);

	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = sizeof(struct panic_data);

	/* Read at offset > size (should return error) */
	params.read_offset = sizeof(struct panic_data) + 1;
	zassert_equal(host_command_process(&args), EC_RES_INVALID_PARAM);
}
