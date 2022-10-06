/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "system.h"

LOG_MODULE_REGISTER(test);

#define BBRAM_REGION_OFF(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), offset)
#define BBRAM_REGION_SIZE(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), size)

static char mock_data[64] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";

FAKE_VALUE_FUNC(uint64_t, cros_system_native_posix_deep_sleep_ticks,
		const struct device *)

static void system_before_after(void *test_data)
{
	RESET_FAKE(cros_system_native_posix_deep_sleep_ticks);
}

ZTEST_SUITE(system, NULL, NULL, system_before_after, system_before_after, NULL);

ZTEST(system, test_bbram_get)
{
	const struct device *const bbram_dev =
		DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));
	uint8_t output[10];
	int rc;

	/* Write expected data to read back */
	rc = bbram_write(bbram_dev, 0, ARRAY_SIZE(mock_data), mock_data);
	zassert_ok(rc, NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD0, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd0),
			  BBRAM_REGION_SIZE(pd0), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD1, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd1),
			  BBRAM_REGION_SIZE(pd1), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD2, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd2),
			  BBRAM_REGION_SIZE(pd2), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(try_slot),
			  BBRAM_REGION_SIZE(try_slot), NULL);
}

ZTEST(system, test_save_read_chip_reset_flags)
{
	uint32_t arbitrary_flags = 0x1234;

	chip_save_reset_flags(0);
	chip_save_reset_flags(arbitrary_flags);
	zassert_equal(chip_read_reset_flags(), arbitrary_flags);
}

ZTEST(system, test_system_set_get_scratchpad)
{
	/* Arbitrary values */
	uint32_t scratch_set = 0x1234;
	uint32_t scratch_read;

	system_set_scratchpad(scratch_set);
	system_get_scratchpad(&scratch_read);
	zassert_equal(scratch_read, scratch_set);
}

ZTEST_USER(system, test_system_console_cmd__idlestats)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	zassert_not_null(sys_dev);

	shell_backend_dummy_clear_output(shell_zephyr);

	k_sleep(K_SECONDS(1));
	zassert_ok(shell_execute_cmd(shell_zephyr, "idlestats"), NULL);

	/* Weakly verify contents */
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_not_equal(buffer_size, 0);
	zassert_not_null(strstr(outbuffer, "Time spent in deep-sleep:"));
	zassert_not_null(strstr(outbuffer, "Total time on:"));

	zassert_equal(cros_system_native_posix_deep_sleep_ticks_fake.call_count,
		      1);
}
