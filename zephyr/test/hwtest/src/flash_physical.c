/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(flash_physical_test, LOG_LEVEL_INF);

ZTEST_SUITE(flash_physical, NULL, NULL, NULL, NULL, NULL);

struct flash_info {
	int num_flash_banks;
	int write_protect_bank_offset;
	int write_protect_bank_count;
};

/*
 * flash_config coverage only; see Zephyr "flash_stm32" for upstream STM32 flash
 * tests.
 */
#if defined(CONFIG_SOC_FAMILY_STM32)
struct flash_info flash_info = {
	/*
	 * 12 sectors listed in STM32F412CG (Bloonchipper) layout:
	 * https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint-ram-and-flash.md#stm32f412cg-bloonchipper
	 */
	.num_flash_banks = 12,
	.write_protect_bank_offset = 0,
	.write_protect_bank_count = 5,
};
#elif defined(CONFIG_SOC_FAMILY_NPCX)
struct flash_info flash_info = {
	.num_flash_banks = 16,
	.write_protect_bank_offset = 0,
	/* Helipilot's CONFIG_RO_SIZE is 128 KB and write protect size is 64 KB,
	 * so there should be 2 banks
	 */
	.write_protect_bank_count = 2,
};
#else
#error "Flash config tests not defined for this chip. Please add it."
#endif

ZTEST(flash_physical, test_flash_config)
{
	zassert_equal(crec_flash_total_banks(), flash_info.num_flash_banks,
		      "Flash total banks mismatch");

	zassert_equal(WP_BANK_OFFSET, flash_info.write_protect_bank_offset,
		      "WP bank offset mismatch");

	zassert_equal(WP_BANK_COUNT, flash_info.write_protect_bank_count,
		      "WP bank count mismatch");
}

#if defined(CONFIG_SOC_FAMILY_NPCX)

extern int flash_control_register_locked(const struct device *dev);
extern int cros_flash_npcx_set_write_enable(const struct device *dev);
extern int cros_flash_npcx_set_write_disable(const struct device *dev);
extern void flash_protect_int_flash(const struct device *dev, bool enable);

#define cros_flash_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_flash_controller))

static void flash_physical_npcx_before(void *data)
{
	zassert_not_null(cros_flash_dev, "Flash device instance not found");

	if (!device_is_ready(cros_flash_dev)) {
		zassert_true(false, "Flash device is not ready");
	}

	/* Lock physical flash operations. */
	crec_flash_lock_mapped_storage(1);
}

static void flash_physical_npcx_after(void *data)
{
	/* Guarantee flash is locked down after the test completes or fails. */
	cros_flash_npcx_set_write_disable(cros_flash_dev);

	/* Unlock physical flash operations. */
	crec_flash_lock_mapped_storage(0);
}

ZTEST_SUITE(flash_physical_npcx, NULL, NULL, flash_physical_npcx_before,
	    flash_physical_npcx_after, NULL);

static void test_lock_flash_control_register(void)
{
	int ret;
	int lock_status;

	/* Initial state: Verify it is locked */
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 1,
		      "Flash control register not locked initially");

	/* Unlock the register */
	ret = cros_flash_npcx_set_write_enable(cros_flash_dev);
	zassert_equal(ret, 0, "Failed to enable write: %d", ret);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 0,
		      "Flash control register failed to unlock");

	/* Lock the register */
	ret = cros_flash_npcx_set_write_disable(cros_flash_dev);
	zassert_equal(ret, 0, "Failed to disable write: %d", ret);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 1, "Flash control register failed to lock");

	/* Unlock the register again */
	ret = cros_flash_npcx_set_write_enable(cros_flash_dev);
	zassert_equal(ret, 0, "Failed to enable write: %d", ret);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(
		lock_status, 0,
		"Flash control register failed to unlock on second attempt");
}

static void test_disable_flash_control_register(void)
{
	int ret;
	int lock_status;

	/* Unlock the flash control register */
	ret = cros_flash_npcx_set_write_enable(cros_flash_dev);
	zassert_equal(ret, 0, "Failed to enable write: %d", ret);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 0,
		      "Flash control register failed to unlock");

	/* Disable the flash control register */
	flash_protect_int_flash(cros_flash_dev, /*enable=*/true);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 1, "Flash control register failed to lock");

	/*
	 * Attempt to unlock the flash control register. It should still be
	 * locked because it has been disabled.
	 */
	ret = cros_flash_npcx_set_write_enable(cros_flash_dev);
	zassert_equal(ret, 0, "Failed to enable write: %d", ret);
	lock_status = flash_control_register_locked(cros_flash_dev);
	zassert_true(lock_status >= 0, "Failed to read lock status, error: %d",
		     lock_status);
	zassert_equal(lock_status, 1, "Flash control register failed to lock");
}

/*
 * This test leaves the flash control register in a locked state that
 * persists until a hardware reset. To prevent interference with other tests
 * that require register access, this MUST remain the last test in the suite.
 */
ZTEST(flash_physical_npcx, test_flash_control_register)
{
	LOG_INF("Step 1: Running lock control register test...");
	test_lock_flash_control_register();

	LOG_INF("Step 2: Running disable flash control register test...");
	test_disable_flash_control_register();
}

#endif /* CONFIG_SOC_FAMILY_NPCX */
