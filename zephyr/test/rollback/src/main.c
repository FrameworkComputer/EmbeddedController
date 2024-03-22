/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <console.h>
#include <ec_commands.h>
#include <flash.h>
#include <host_command.h>
#include <rollback.h>
#include <rollback_private.h>
#include <system.h>

#define ROLLBACK0_ADDR DT_REG_ADDR(DT_NODELABEL(rollback0))
#define ROLLBACK0_SIZE DT_REG_SIZE(DT_NODELABEL(rollback0))

#define ROLLBACK1_ADDR DT_REG_ADDR(DT_NODELABEL(rollback1))
#define ROLLBACK1_SIZE DT_REG_SIZE(DT_NODELABEL(rollback1))

FAKE_VALUE_FUNC(int, system_is_locked);

void rollback_before(void)
{
	const struct rollback_data data = {
		.id = 0,
		.rollback_min_version = CONFIG_PLATFORM_EC_ROLLBACK_VERSION,
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE
		.secret = { 0 },
#endif
		.cookie = CROS_EC_ROLLBACK_COOKIE,
	};

	zassert_ok(crec_flash_erase(ROLLBACK0_ADDR, ROLLBACK0_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK0_ADDR, sizeof(data),
				    (const char *)&data));

	zassert_ok(crec_flash_erase(ROLLBACK1_ADDR, ROLLBACK1_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK1_ADDR, sizeof(data),
				    (const char *)&data));

	RESET_FAKE(system_is_locked);
}

ZTEST(rollback, test_rollback_version)
{
	struct rollback_data rollback;

	/* Make sure minimum version in rollabck is 0. */
	zassert_equal(rollback_get_minimum_version(), 0);

	/* Update rollback version to 1, it will initialize the second region.
	 */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);
	zassert_equal(rollback_get_minimum_version(), 1);

	/* Make sure rollback version is stored in the second rollback region */
	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 1);
	zassert_equal(rollback.rollback_min_version, 1);

	/*
	 * Update rollback version to 2, it will initialize the second
	 * region.
	 */
	zassert_equal(rollback_update_version(2), EC_SUCCESS);
	zassert_equal(rollback_get_minimum_version(), 2);

	/* Make sure rollback version is stored in the second rollback region */
	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 2);
	zassert_equal(rollback.rollback_min_version, 2);

	/* Try to go back to 1. It should return EC_ERROR_INVAL. */
	zassert_equal(rollback_update_version(1), EC_ERROR_INVAL);
	zassert_equal(rollback_get_minimum_version(), 2);

	/* Make sure rollback regions remain unchanged */
	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 1);
	zassert_equal(rollback.rollback_min_version, 1);

	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 2);
	zassert_equal(rollback.rollback_min_version, 2);
}

ZTEST(rollback, test_entropy_trivial)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];

	/*
	 * When no rollback region is initialized (the secret is 0x00 32 times)
	 * an attempt to get secret will result in error.
	 */
	zassert_equal(rollback_get_secret(secret), EC_ERROR_UNKNOWN);
}

ZTEST(rollback, test_add_entropy)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];
	struct rollback_data rollback;

	/*
	 * At the beginning, the secret is 0x00 32 times. New entropy is just
	 * SHA256(old_entropy + data).
	 *
	 * Let's add "some_rollback_entropy" (736f6d655f726f6c6c6261636b5f656e74
	 * 726f7079) to our pool.
	 */
	const char data1[] = "some_rollback_entropy";

	/* Don't include NULL character from the end of the string. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/*
	 * SHA256(00000000000000000000000000000000000000000000000000000000000000
	 * 00736f6d655f726f6c6c6261636b5f656e74726f7079) = 3ce9c8011d3f98d96fa7
	 * 41da4f10f2f410d80372ebba98ff726b521338e6cfd9
	 */
	const uint8_t entropy1[] = { 0x3c, 0xe9, 0xc8, 0x01, 0x1d, 0x3f, 0x98,
				     0xd9, 0x6f, 0xa7, 0x41, 0xda, 0x4f, 0x10,
				     0xf2, 0xf4, 0x10, 0xd8, 0x03, 0x72, 0xeb,
				     0xba, 0x98, 0xff, 0x72, 0x6b, 0x52, 0x13,
				     0x38, 0xe6, 0xcf, 0xd9 };
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
	zassert_mem_equal(secret, entropy1, sizeof(secret));

	/* Make sure secret is stored in the second rollback region */
	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 1);
	zassert_mem_equal(rollback.secret, entropy1, sizeof(rollback.secret));

	/* Next, we will add "lalala" (6c616c616c61) string to the pool */
	const char data2[] = "lalala";

	/* Don't include NULL character from the end of the string. */
	zassert_equal(rollback_add_entropy(data2, sizeof(data2) - 1),
		      EC_SUCCESS);

	/*
	 * SHA256(3ce9c8011d3f98d96fa741da4f10f2f410d80372ebba98ff726b521338e6cf
	 * d96c616c616c61) = bb5d1789c4dd45d831752ce5b59bbdfbdbdc1fc474bb454385
	 * 84a372cad85559
	 */
	const uint8_t entropy2[] = { 0xbb, 0x5d, 0x17, 0x89, 0xc4, 0xdd, 0x45,
				     0xd8, 0x31, 0x75, 0x2c, 0xe5, 0xb5, 0x9b,
				     0xbd, 0xfb, 0xdb, 0xdc, 0x1f, 0xc4, 0x74,
				     0xbb, 0x45, 0x43, 0x85, 0x84, 0xa3, 0x72,
				     0xca, 0xd8, 0x55, 0x59 };
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
	zassert_mem_equal(secret, entropy2, sizeof(secret));

	/* Make sure secret is stored in the first rollback region */
	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.id, 2);
	zassert_mem_equal(rollback.secret, entropy2, sizeof(rollback.secret));
}

ZTEST(rollback, test_version_update_copy_secret)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];
	struct rollback_data rollback;
	const char data1[] = "some_rollback_entropy";
	const uint8_t entropy1[] = { 0x3c, 0xe9, 0xc8, 0x01, 0x1d, 0x3f, 0x98,
				     0xd9, 0x6f, 0xa7, 0x41, 0xda, 0x4f, 0x10,
				     0xf2, 0xf4, 0x10, 0xd8, 0x03, 0x72, 0xeb,
				     0xba, 0x98, 0xff, 0x72, 0x6b, 0x52, 0x13,
				     0x38, 0xe6, 0xcf, 0xd9 };

	/* Add some entropy to rollback region. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/* Update minimum rollback version to 1. */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);

	/* Check that secret is correct. */
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
	zassert_mem_equal(secret, entropy1, sizeof(secret));

	/* Make sure both regions have the same secret value. */
	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_mem_equal(rollback.secret, entropy1, sizeof(rollback.secret));

	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_mem_equal(rollback.secret, entropy1, sizeof(rollback.secret));
}

ZTEST(rollback, test_add_entropy_copy_minimal_version)
{
	struct rollback_data rollback;
	const char data1[] = "some_rollback_entropy";

	/* Set minimum rollback version to 1. */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);

	/* Add some entropy to rollback region. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/* Check that minimal rollback version is correct. */
	zassert_equal(rollback_get_minimum_version(), 1);

	/* Make sure both regions have the minimum version. */
	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.rollback_min_version, 1);

	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_equal(rollback.rollback_min_version, 1);
}

ZTEST(rollback, test_hostcmd_rollback_info)
{
	struct ec_response_rollback_info response;

	zassert_ok(ec_cmd_rollback_info(NULL, &response));
	zassert_equal(response.id, 0);
	zassert_equal(response.rollback_min_version, 0);

	/* Update minimum rollback version to 1. */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);

	/* Make sure correct rollback minimum version is returned. */
	zassert_ok(ec_cmd_rollback_info(NULL, &response));
	zassert_equal(response.id, 1);
	zassert_equal(response.rollback_min_version, 1);

	/* Update minimum rollback version to 2. */
	zassert_equal(rollback_update_version(2), EC_SUCCESS);

	/* Make sure correct rollback minimum version is returned. */
	zassert_ok(ec_cmd_rollback_info(NULL, &response));
	zassert_equal(response.id, 2);
	zassert_equal(response.rollback_min_version, 2);
}

ZTEST(rollback, test_hostcmd_add_entropy)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];
	struct ec_params_rollback_add_entropy params;

	/* Add some entropy from RNG. */
	params.action = ADD_ENTROPY_ASYNC;
	zassert_ok(ec_cmd_add_entropy(NULL, &params));

	/*
	 * Check that EC_RES_BUSY will be returned if operation
	 * is not finished.
	 */
	params.action = ADD_ENTROPY_GET_RESULT;
	zassert_equal(ec_cmd_add_entropy(NULL, &params), EC_RES_BUSY);

	/* Give hook task opportunity to run the operation */
	k_usleep(1000);

	/* Check the result of the operation. */
	params.action = ADD_ENTROPY_GET_RESULT;
	zassert_ok(ec_cmd_add_entropy(NULL, &params));

	/* Confirm that the secret is not trivial. */
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
}

ZTEST(rollback, test_hostcmd_add_entropy_reset)
{
	struct rollback_data rollback;
	struct ec_params_rollback_add_entropy params;
	const char data1[] = "some_rollback_entropy";
	const uint8_t entropy1[] = { 0x3c, 0xe9, 0xc8, 0x01, 0x1d, 0x3f, 0x98,
				     0xd9, 0x6f, 0xa7, 0x41, 0xda, 0x4f, 0x10,
				     0xf2, 0xf4, 0x10, 0xd8, 0x03, 0x72, 0xeb,
				     0xba, 0x98, 0xff, 0x72, 0x6b, 0x52, 0x13,
				     0x38, 0xe6, 0xcf, 0xd9 };

	/* Add some entropy to rollback region. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/* Request entropy reset. */
	params.action = ADD_ENTROPY_RESET_ASYNC;
	zassert_ok(ec_cmd_add_entropy(NULL, &params));

	/* Give hook task opportunity to run the operation */
	k_usleep(1000);

	/* Check the result of the operation. */
	params.action = ADD_ENTROPY_GET_RESULT;
	zassert_ok(ec_cmd_add_entropy(NULL, &params));

	/* Confirm that no region contain previous entropy after reset. */
	zassert_ok(crec_flash_read(ROLLBACK0_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_not_equal(
		memcmp(rollback.secret, entropy1, sizeof(rollback.secret)), 0);

	zassert_ok(crec_flash_read(ROLLBACK1_ADDR, sizeof(rollback),
				   (char *)&rollback));
	zassert_not_equal(
		memcmp(rollback.secret, entropy1, sizeof(rollback.secret)), 0);
}

ZTEST(rollback, test_console_rollbackinfo_system_unlocked)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	/* Arbitrary array size for sprintf should not need this amount */
	char format_buffer[100];
	const char data1[] = "some_rollback_entropy";

	/* Add some entropy to rollback region. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/* Update minimum rollback version to 1. */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);

	system_is_locked_fake.return_val = false;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "rollbackinfo"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(strstr(outbuffer, "rollback minimum version: 1"));

	sprintf(format_buffer, "RW rollback version: %d",
		system_get_rollback_version(EC_IMAGE_RW));
	zassert_not_null(strstr(outbuffer, format_buffer));

	sprintf(format_buffer, "rollback %d: %08x %08x %08x [%02x..%02x] *",
		/* region */ 0,
		/* id */ 2,
		/* minimum version */ 1, CROS_EC_ROLLBACK_COOKIE,
		/* first byte of secret */ 0x3c,
		/* last byte of secret */ 0xd9);
	zassert_not_null(strstr(outbuffer, format_buffer));

	sprintf(format_buffer, "rollback %d: %08x %08x %08x [%02x..%02x]",
		/* region */ 1,
		/* id */ 1,
		/* minimum version */ 0, CROS_EC_ROLLBACK_COOKIE,
		/* first byte of secret */ 0x3c,
		/* last byte of secret */ 0xd9);
	zassert_not_null(strstr(outbuffer, format_buffer));
}

ZTEST(rollback, test_console_rollbackinfo_system_locked)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	/* Arbitrary array size for sprintf should not need this amount */
	char format_buffer[100];
	const char data1[] = "some_rollback_entropy";

	/* Add some entropy to rollback region. */
	zassert_equal(rollback_add_entropy(data1, sizeof(data1) - 1),
		      EC_SUCCESS);

	/* Update minimum rollback version to 1. */
	zassert_equal(rollback_update_version(1), EC_SUCCESS);

	system_is_locked_fake.return_val = true;

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "rollbackinfo"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(buffer_size > 0, NULL);

	sprintf(format_buffer, "rollback %d: %08x %08x %08x *",
		/* region */ 0,
		/* id */ 2,
		/* minimum version */ 1, CROS_EC_ROLLBACK_COOKIE);
	zassert_not_null(strstr(outbuffer, format_buffer));

	sprintf(format_buffer, "rollback %d: %08x %08x %08x",
		/* region */ 1,
		/* id */ 1,
		/* minimum version */ 0, CROS_EC_ROLLBACK_COOKIE);
	zassert_not_null(strstr(outbuffer, format_buffer));

	/* Make sure there is no secret in the output. */
	sprintf(format_buffer, "[%02x..%02x]",
		/* first byte of secret */ 0x3c,
		/* last byte of secret */ 0xd9);
	zassert_is_null(strstr(outbuffer, format_buffer));
}

ZTEST(rollback, test_console_rollbackupdate)
{
	zassert_equal(shell_execute_cmd(get_ec_shell(), "rollbackupdate 1"),
		      EC_SUCCESS);

	/* Make sure rollback minimum version was updated */
	zassert_equal(rollback_get_minimum_version(), 1);
}

ZTEST(rollback, test_console_rollbackupdate_bad_parameters)
{
	zassert_equal(shell_execute_cmd(get_ec_shell(), "rollbackupdate"),
		      EC_ERROR_PARAM_COUNT);
	zassert_equal(shell_execute_cmd(get_ec_shell(), "rollbackupdate -1"),
		      EC_ERROR_PARAM1);
	zassert_equal(shell_execute_cmd(get_ec_shell(), "rollbackupdate abc"),
		      EC_ERROR_PARAM1);

	/* Make sure that rollback minimum version was not changed. */
	zassert_equal(rollback_get_minimum_version(), 0);
}

ZTEST(rollback, test_console_rollbackaddent_rng)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];

	zassert_equal(shell_execute_cmd(get_ec_shell(), "rollbackaddent"),
		      EC_SUCCESS);

	/* Confirm that the secret is not trivial. */
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
}

ZTEST(rollback, test_console_rollbackaddent_provided)
{
	uint8_t secret[CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE];
	const uint8_t entropy1[] = { 0x3c, 0xe9, 0xc8, 0x01, 0x1d, 0x3f, 0x98,
				     0xd9, 0x6f, 0xa7, 0x41, 0xda, 0x4f, 0x10,
				     0xf2, 0xf4, 0x10, 0xd8, 0x03, 0x72, 0xeb,
				     0xba, 0x98, 0xff, 0x72, 0x6b, 0x52, 0x13,
				     0x38, 0xe6, 0xcf, 0xd9 };

	zassert_equal(shell_execute_cmd(get_ec_shell(),
					"rollbackaddent some_rollback_entropy"),
		      EC_SUCCESS);

	/* Confirm that the secret is correct. */
	zassert_equal(rollback_get_secret(secret), EC_SUCCESS);
	zassert_mem_equal(secret, entropy1, sizeof(secret));
}

void *rollback_setup(void)
{
	/* Wait for the shell to start. */
	k_sleep(K_MSEC(1));
	zassert_equal(get_ec_shell()->ctx->state, SHELL_STATE_ACTIVE, NULL);

	return NULL;
}

ZTEST_SUITE(rollback, NULL, rollback_setup, rollback_before, NULL, NULL);
