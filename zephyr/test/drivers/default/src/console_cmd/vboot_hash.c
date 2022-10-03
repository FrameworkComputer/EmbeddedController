/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <string.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "printf.h"
#include "sha256.h"
#include "system.h"
#include "test/drivers/test_state.h"
#include "vboot_hash.h"

#define CUSTOM_HASH_LENGTH (32)

struct console_cmd_hash_fixture {
	uint8_t rw_hash[SHA256_DIGEST_SIZE];
	uint8_t ro_hash[SHA256_DIGEST_SIZE];
};

ZTEST_F(console_cmd_hash, get_rw)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Start calculating the RW image hash */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash rw"), NULL);

	/* Wait for completion */
	WAIT_FOR(!vboot_hash_in_progress(), 1000000, k_sleep(K_MSEC(10)));

	/* Call again with no args to see the resulting hash */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* The RW hash should be reported. */
	char hash_buf[hex_str_buf_size(SHA256_DIGEST_SIZE)];

	snprintf_hex_buffer(hash_buf, sizeof(hash_buf),
			    HEX_BUF(fixture->rw_hash, SHA256_DIGEST_SIZE));

	zassert_ok(!strstr(outbuffer, hash_buf), "Output was: `%s`", outbuffer);
}

ZTEST_F(console_cmd_hash, get_ro)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Start calculating the RW image hash */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash ro"), NULL);

	/* Wait for completion */
	WAIT_FOR(!vboot_hash_in_progress(), 1000000, k_sleep(K_MSEC(10)));

	/* Call again with no args to see the resulting hash */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* The RO hash should be reported. */
	char hash_buf[hex_str_buf_size(SHA256_DIGEST_SIZE)];

	snprintf_hex_buffer(hash_buf, sizeof(hash_buf),
			    HEX_BUF(fixture->ro_hash, SHA256_DIGEST_SIZE));

	zassert_ok(!strstr(outbuffer, hash_buf), "Output was: `%s`", outbuffer);
}

ZTEST_F(console_cmd_hash, abort)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Start calculating the RO image hash */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash ro"), NULL);

	/* Immediately cancel */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash abort"), NULL);

	/* Call again with no args to check status */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Depending on timing, we should see one of these statuses */
	zassert_true(strstr(outbuffer, "(invalid)") ||
			     strstr(outbuffer, "(aborting)"),
		     "Output was: `%s`", outbuffer);
}

ZTEST_F(console_cmd_hash, custom_range)
{
	char command[256];
	uint32_t offset = flash_get_rw_offset(system_get_active_copy());

	/* Hash an arbitrary portion of the flash */
	snprintf(command, sizeof(command), "hash 0x%x %u", offset,
		 CUSTOM_HASH_LENGTH);
	zassert_ok(shell_execute_cmd(get_ec_shell(), command));

	WAIT_FOR(!vboot_hash_in_progress(), 1000000, k_sleep(K_MSEC(10)));

	/* Calculate the expected hash */
	struct sha256_ctx hash_ctx;
	uint8_t *hash;
	uint8_t buf[CUSTOM_HASH_LENGTH];

	zassert_ok(crec_flash_read(offset, sizeof(buf), buf));
	SHA256_init(&hash_ctx);
	SHA256_update(&hash_ctx, buf, sizeof(buf));
	hash = SHA256_final(&hash_ctx);

	/* Call again with no args to check status */
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Compare hashes */
	char hash_buf[hex_str_buf_size(SHA256_DIGEST_SIZE)];

	snprintf_hex_buffer(hash_buf, sizeof(hash_buf),
			    HEX_BUF(hash, SHA256_DIGEST_SIZE));

	zassert_ok(!strstr(outbuffer, hash_buf), "Output was: `%s`. Actual: %s",
		   outbuffer, hash_buf);
}

ZTEST_F(console_cmd_hash, custom_range_with_nonce)
{
	char command[256];
	uint32_t offset = flash_get_rw_offset(system_get_active_copy());
	int nonce = 1234;

	/* Hash an arbitrary portion of the flash w/ nonce */
	snprintf(command, sizeof(command), "hash 0x%x %u %d", offset,
		 CUSTOM_HASH_LENGTH, nonce);
	zassert_ok(shell_execute_cmd(get_ec_shell(), command));

	WAIT_FOR(!vboot_hash_in_progress(), 1000000, k_sleep(K_MSEC(10)));

	/* Calculate the expected hash */
	struct sha256_ctx hash_ctx;
	uint8_t *hash;
	uint8_t buf[CUSTOM_HASH_LENGTH];

	zassert_ok(crec_flash_read(offset, sizeof(buf), buf));
	SHA256_init(&hash_ctx);
	SHA256_update(&hash_ctx, (const uint8_t *)&nonce, sizeof(nonce));
	SHA256_update(&hash_ctx, buf, sizeof(buf));
	hash = SHA256_final(&hash_ctx);

	/* Call again with no args to check status */
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "hash"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Compare hashes */
	char hash_buf[hex_str_buf_size(SHA256_DIGEST_SIZE)];

	snprintf_hex_buffer(hash_buf, sizeof(hash_buf),
			    HEX_BUF(hash, SHA256_DIGEST_SIZE));

	zassert_ok(!strstr(outbuffer, hash_buf), "Output was: `%s`. Actual: %s",
		   outbuffer, hash_buf);
}

ZTEST(console_cmd_hash, invalid)
{
	/* Invalid subcommand */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "hash foo"));

	/* For custom ranges, non-numbers are invalid */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "hash a b"));
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "hash 1 b"));
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "hash 1 2 c"));
}

static void *setup(void)
{
	static struct console_cmd_hash_fixture f;

	return &f;
}

static void before(void *data)
{
	struct console_cmd_hash_fixture *f =
		(struct console_cmd_hash_fixture *)data;
	const uint8_t *hash_ptr;
	int rv;

	/* Stop and clear current hash */
	vboot_hash_abort();

	/* Get the RW hash and save it to our fixture */
	rv = vboot_get_rw_hash(&hash_ptr);
	zassert_ok(rv, "Got %d", rv);
	memcpy(f->rw_hash, hash_ptr, sizeof(f->rw_hash));

	/* Compute the RO hash, too */
	rv = vboot_get_ro_hash(&hash_ptr);
	zassert_ok(rv, "Got %d", rv);
	memcpy(f->ro_hash, hash_ptr, sizeof(f->ro_hash));
}

static void after(void *data)
{
	ARG_UNUSED(data);

	/* Stop and clear current hash */
	vboot_hash_abort();

	/* Wait a moment to allow the hashing to stop */
	k_sleep(K_MSEC(100));
}

ZTEST_SUITE(console_cmd_hash, drivers_predicate_post_main, setup, before, after,
	    NULL);
