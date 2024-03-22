/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bbram.h"
#include "drivers/cros_system.h"
#include "fakes.h"
#include "system.h"

#include <setjmp.h>

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

LOG_MODULE_REGISTER(test);

static char mock_data[64] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";

int system_preinitialize(const struct device *unused);

ZTEST(system, test_invalid_bbram_index)
{
	zassert_equal(EC_ERROR_INVAL,
		      system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT + 1, NULL));
}

ZTEST(system, test_bbram_get)
{
	const struct device *const bbram_dev =
		DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));
	uint8_t output[10];
	int rc;

	/* Write expected data to read back */
	rc = bbram_write(bbram_dev, 0, ARRAY_SIZE(mock_data), mock_data);
	zassert_ok(rc);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD0, output);
	zassert_ok(rc);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFFSET(pd0),
			  BBRAM_REGION_SIZE(pd0), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD1, output);
	zassert_ok(rc);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFFSET(pd1),
			  BBRAM_REGION_SIZE(pd1), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD2, output);
	zassert_ok(rc);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFFSET(pd2),
			  BBRAM_REGION_SIZE(pd2), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, output);
	zassert_ok(rc);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFFSET(try_slot),
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

ZTEST(system, test_system_get_scratchpad_fail)
{
	const struct device *bbram_dev =
		DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));

	zassert_ok(bbram_emul_set_invalid(bbram_dev, true));
	zassert_equal(-EC_ERROR_INVAL, system_get_scratchpad(NULL));
}

static jmp_buf jmp_hibernate;

static int _test_cros_system_native_posix_hibernate(const struct device *dev,
						    uint32_t seconds,
						    uint32_t microseconds)
{
	longjmp(jmp_hibernate, 1);

	return 0;
}

ZTEST(system, test_system_hibernate)
{
	/*
	 * Due to setjmp usage, this test provides no coverage, but does
	 * actually cover the code. This is due to a bug in LCOV.
	 */
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	int ret = setjmp(jmp_hibernate);
	/* Validate 0th and last bit preserved*/
	uint32_t secs = BIT(31) + 1;
	uint32_t msecs = BIT(31) + 3;

	zassert_not_null(sys_dev);

	cros_system_native_posix_hibernate_fake.custom_fake =
		_test_cros_system_native_posix_hibernate;

	if (ret == 0) {
		system_hibernate(secs, msecs);
	}

	zassert_not_equal(ret, 0);

	zassert_equal(cros_system_native_posix_hibernate_fake.call_count, 1);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg0_val,
		      sys_dev);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg1_val, secs);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg2_val, msecs);
	zassert_equal(board_hibernate_fake.call_count, 1);
}

ZTEST(system, test_system_hibernate__failure)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	/* Validate 0th and last bit preserved*/
	uint32_t secs = BIT(31) + 1;
	uint32_t msecs = BIT(31) + 3;

	zassert_not_null(sys_dev);

	cros_system_native_posix_hibernate_fake.return_val = -1;

	system_hibernate(secs, msecs);

	zassert_equal(cros_system_native_posix_hibernate_fake.call_count, 1);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg0_val,
		      sys_dev);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg1_val, secs);
	zassert_equal(cros_system_native_posix_hibernate_fake.arg2_val, msecs);
}

ZTEST(system, test_system_get_chip_values)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");

	zassert_not_null(sys_dev);

	/* Vendor */
	cros_system_native_posix_get_chip_vendor_fake.return_val = "a";
	zassert_mem_equal(system_get_chip_vendor(), "a", sizeof("a"));
	zassert_equal(cros_system_native_posix_get_chip_vendor_fake.call_count,
		      1);
	zassert_equal(cros_system_native_posix_get_chip_vendor_fake.arg0_val,
		      sys_dev);

	/* Name */
	cros_system_native_posix_get_chip_name_fake.return_val = "b";
	zassert_mem_equal(system_get_chip_name(), "b", sizeof("b"));
	zassert_equal(cros_system_native_posix_get_chip_name_fake.call_count,
		      1);
	zassert_equal(cros_system_native_posix_get_chip_name_fake.arg0_val,
		      sys_dev);

	/* Revision */
	cros_system_native_posix_get_chip_revision_fake.return_val = "c";
	zassert_mem_equal(system_get_chip_revision(), "c", sizeof("c"));
	zassert_equal(
		cros_system_native_posix_get_chip_revision_fake.call_count, 1);
	zassert_equal(cros_system_native_posix_get_chip_revision_fake.arg0_val,
		      sys_dev);
}

static int _test_cros_system_native_posix_soc_reset(const struct device *dev)
{
	printf("called from soc reset");
	longjmp(jmp_hibernate, 1);

	return 0;
}

ZTEST(system, test_system_reset)
{
	/*
	 * Despite using setjmp this test consistently covers the code under
	 * test. Context: https://github.com/llvm/llvm-project/issues/50119
	 */
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	int ret = setjmp(jmp_hibernate);
	uint32_t arbitrary_flags_w_reset_wait_ext = 0x1234 |
						    SYSTEM_RESET_WAIT_EXT;
	uint32_t encoded_arbitrary_flags_w_reset_wait_ext;

	system_encode_save_flags(arbitrary_flags_w_reset_wait_ext,
				 &encoded_arbitrary_flags_w_reset_wait_ext);

	zassert_not_null(sys_dev);

	cros_system_native_posix_soc_reset_fake.custom_fake =
		_test_cros_system_native_posix_soc_reset;

	if (ret == 0) {
		system_reset(arbitrary_flags_w_reset_wait_ext);
	}

	zassert_not_null(sys_dev);

	zassert_equal(chip_read_reset_flags(),
		      encoded_arbitrary_flags_w_reset_wait_ext);

	zassert_equal(watchdog_reload_fake.call_count, 1000);
	zassert_equal(cros_system_native_posix_soc_reset_fake.call_count, 1);
	zassert_equal(cros_system_native_posix_soc_reset_fake.arg0_val,
		      sys_dev);
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

ZTEST(system, test_init_invalid_reset_cause)
{
	cros_system_native_posix_get_reset_cause_fake.return_val = -1;
	zassert_equal(-1, system_preinitialize(NULL));
}

ZTEST(system, test_init_cause_vcc1_rst_pin)
{
	cros_system_native_posix_get_reset_cause_fake.return_val = VCC1_RST_PIN;
	chip_save_reset_flags(0);
	system_clear_reset_flags(0xffffffff);

	zassert_ok(system_preinitialize(NULL));
	zassert_equal(EC_RESET_FLAG_RESET_PIN, system_get_reset_flags());

	chip_save_reset_flags(EC_RESET_FLAG_INITIAL_PWR);
	zassert_ok(system_preinitialize(NULL));
	zassert_equal(EC_RESET_FLAG_RESET_PIN | EC_RESET_FLAG_POWER_ON |
			      EC_RESET_FLAG_POWER_ON,
		      system_get_reset_flags());
}

ZTEST(system, test_init_cause_debug_rst)
{
	cros_system_native_posix_get_reset_cause_fake.return_val = DEBUG_RST;
	chip_save_reset_flags(0);
	system_clear_reset_flags(0xffffffff);

	zassert_ok(system_preinitialize(NULL));
	zassert_equal(EC_RESET_FLAG_SOFT, system_get_reset_flags());
}

ZTEST(system, test_init_cause_watchdog_rst)
{
	cros_system_native_posix_get_reset_cause_fake.return_val = WATCHDOG_RST;
	chip_save_reset_flags(0);
	system_clear_reset_flags(0xffffffff);

	zassert_ok(system_preinitialize(NULL));
	zassert_equal(EC_RESET_FLAG_WATCHDOG, system_get_reset_flags());
}
