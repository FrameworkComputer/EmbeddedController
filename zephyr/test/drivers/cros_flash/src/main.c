/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../../../../drivers/cros_flash/cros_flash.h"
#include "drivers/cros_flash.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio_signal.h"
#include "system.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define WP_L_GPIO_PATH NAMED_GPIOS_GPIO_NODE(wp_l)

static int gpio_wp_l_set(int value)
{
	const struct device *wp_l_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(WP_L_GPIO_PATH, gpios));

	return gpio_emul_input_set(wp_l_gpio_dev,
				   DT_GPIO_PIN(WP_L_GPIO_PATH, gpios), value);
}

#define cros_flash_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_flash_controller))

FAKE_VALUE_FUNC(int, flash_change_wp, const struct device *, uint32_t,
		uint32_t);
FAKE_VALUE_FUNC(int, flash_get_wp, const struct device *, uint32_t *);
FAKE_VALUE_FUNC(int, flash_change_rdp, const struct device *, bool, bool);
FAKE_VALUE_FUNC(int, flash_get_rdp, const struct device *, bool *, bool *);
FAKE_VALUE_FUNC(int, flash_block_protection_changes, const struct device *);
FAKE_VALUE_FUNC(int, flash_block_control_access, const struct device *);
FAKE_VALUE_FUNC(const uint8_t *, system_get_jump_tag, uint16_t, int *, int *);

static uint32_t protected_mask;
static bool protection_changes_blocked;
static bool control_access_blocked;
static bool rdp_enabled;

static struct cros_flash_protection current_protection;
static int protection_struct_version;

static const uint8_t *system_get_jump_tag_custom_fake(uint16_t tag,
						      int *version, int *size)
{
	if (tag != FLASH_SYSJUMP_TAG)
		return NULL;
	if (version)
		*version = protection_struct_version;
	if (size)
		*size = sizeof(current_protection);
	return (uint8_t *)&current_protection;
}

static int flash_block_protection_changes_custom_fake(const struct device *dev)
{
	protection_changes_blocked = true;

	return 0;
}

static int flash_block_control_access_custom_fake(const struct device *dev)
{
	control_access_blocked = true;

	return 0;
}

static int flash_get_wp_custom_fake(const struct device *dev, uint32_t *wp_mask)
{
	*wp_mask = protected_mask;

	return 0;
}

static int flash_change_wp_custom_fake(const struct device *dev,
				       uint32_t disable_mask,
				       uint32_t enable_mask)
{
	if (protection_changes_blocked)
		ztest_test_fail();

	protected_mask &= ~disable_mask;
	protected_mask |= enable_mask;

	return 0;
}

static int flash_get_rdp_custom_fake(const struct device *dev, bool *enabled,
				     bool *permanent)
{
	if (enabled)
		*enabled = rdp_enabled;

	if (permanent)
		*permanent = false;

	return 0;
}

static int flash_change_rdp_custom_fake(const struct device *dev, bool enable,
					bool permanent)
{
	/*
	 * We don't support disabling RDP, enabling RDP permanently or
	 * protection changes are blocked.
	 */
	if ((!enable && rdp_enabled) || permanent || protection_changes_blocked)
		ztest_test_fail();

	rdp_enabled = enable;

	return 0;
}

ZTEST_USER(cros_flash, test_init_no_hwwp_no_protection)
{
	cros_flash_init(cros_flash_dev);

	/* Check that there are no protection changes. */
	zassert_equal(flash_change_wp_fake.call_count, 0);
	zassert_equal(flash_change_rdp_fake.call_count, 0);
	zassert_equal(flash_block_control_access_fake.call_count, 0);
	zassert_equal(flash_block_protection_changes_fake.call_count, 0);

	/* Check that no reset was requested. */
	zassert_equal(system_reset_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_no_hwwp_protected_wp_region_success)
{
	/* First sector that belongs to WP region is protected. */
	protected_mask = 0x1;

	cros_flash_init(cros_flash_dev);

	/* Expect that write protection was disabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1, "flash_change_wp %d",
		      flash_change_wp_fake.call_count);
	zassert_equal(protected_mask, 0);

	/* Check if no other protection changes. */
	zassert_equal(flash_change_rdp_fake.call_count, 0);

	/* Check that reset was requested. */
	zassert_equal(system_reset_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_init_no_hwwp_disabling_wp_failure_no_reboot)
{
	flash_change_wp_fake.return_val = -EINVAL;
	flash_change_wp_fake.custom_fake = NULL;

	cros_flash_init(cros_flash_dev);

	/* Check that reset was NOT requested. It would lead to reboot loop */
	zassert_equal(system_reset_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_no_hwwp_enabled_rdp_is_not_disabled)
{
	rdp_enabled = true;

	cros_flash_init(cros_flash_dev);

	/* Check that there was no attempt to change RDP */
	zassert_equal(flash_change_rdp_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_no_hwwp_rdp_enabled_no_reboot)
{
	rdp_enabled = true;

	cros_flash_init(cros_flash_dev);

	/* Check that reset was NOT requested. It would lead to reboot loop */
	zassert_equal(system_reset_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_hwwp_no_ro_at_boot_no_protection)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_init(cros_flash_dev);

	/*
	 * Expect that WP region is not protected, because RO_AT_BOOT is not
	 * enabled.
	 */
	zassert_equal(flash_change_wp_fake.call_count, 0);
	zassert_equal(protected_mask, 0);

	/* Check that there was no attempt to change RDP. */
	zassert_equal(flash_change_rdp_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_hwwp_no_ro_at_boot_no_reboot)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_init(cros_flash_dev);

	/* Check that reset was not requested. */
	zassert_equal(system_reset_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_hwwp_no_ro_at_boot_option_bytes_enabled)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_init(cros_flash_dev);

	/*
	 * Check that option register was not disabled. If RO_AT_BOOT is not
	 * enabled we allow to boot without disabling option register, so we
	 * can enable protection later.
	 */
	zassert_equal(flash_block_protection_changes_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_hwwp_no_ro_at_boot_disables_protection)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	/* Protect WP region. */
	protected_mask = 0xffff;

	cros_flash_init(cros_flash_dev);

	/* Expect that WP region protection is disabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0);

	/* Check that reset was requested. */
	zassert_equal(system_reset_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_init_hwwp_rdp_and_wp_enabled_no_reboot)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	/* Enable RDP. It's used as a PSTATE (source of RO_AT_BOOT flag) */
	rdp_enabled = true;

	/* Protect WP region */
	protected_mask = 0xffff;

	cros_flash_init(cros_flash_dev);

	/* Expect that WP region is still protected. */
	zassert_equal(flash_change_wp_fake.call_count, 0);
	zassert_equal(protected_mask, 0xffff);

	/* Check that reset was not requested. */
	zassert_equal(system_reset_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_init_hwwp_rdp_and_wp_enabled_disables_option)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	/* Enable RDP. It's used as a PSTATE (source of RO_AT_BOOT flag) */
	rdp_enabled = true;

	/* Protect WP region */
	protected_mask = 0xffff;

	cros_flash_init(cros_flash_dev);

	/*
	 * Check that option register was disabled. It means that we can't
	 * change protection later.
	 */
	zassert_equal(flash_block_protection_changes_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_init_hwwp_rdp_enabled_wp_disabled)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	/* Enable RDP. It's used as a PSTATE (source of RO_AT_BOOT flag). */
	rdp_enabled = true;

	cros_flash_init(cros_flash_dev);

	/* Expect that WP region is protected. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffff);

	/* Check that reset was requested. */
	zassert_equal(system_reset_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_init_hwwp_rdp_enabled_wp_get_failure)
{
	/* Enable HW WP. */
	gpio_wp_l_set(0);

	/* Enable RDP. It's used as a PSTATE (source of RO_AT_BOOT flag). */
	rdp_enabled = true;

	/* Simulate failure to get write protection status */
	flash_get_wp_fake.return_val = -EINVAL;
	flash_get_wp_fake.custom_fake = NULL;

	cros_flash_init(cros_flash_dev);

	/* Expect that WP region is protected. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffff);

	/* Check that reset was requested. */
	zassert_equal(system_reset_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_init_decode_jump_data_no_data)
{
	/* Tell cros_flash_init() that it should restore data */
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);

	system_get_jump_tag_fake.return_val = NULL;
	system_get_jump_tag_fake.custom_fake = NULL;

	zassert_equal(cros_flash_init(cros_flash_dev), -ENODATA);
}

ZTEST_USER(cros_flash, test_init_decode_jump_data_wrong_version)
{
	/* Tell cros_flash_init() that it should restore data */
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);
	protection_struct_version = 2;

	zassert_equal(cros_flash_init(cros_flash_dev), -ENOENT);
}

ZTEST_USER(cros_flash, test_init_decode_jump_data_success)
{
	/* Tell cros_flash_init() that it should restore data */
	system_set_reset_flags(EC_RESET_FLAG_SYSJUMP);

	current_protection.control_access_blocked = true;
	current_protection.protection_changes_blocked = true;

	zassert_equal(cros_flash_init(cros_flash_dev), 0);

	/* Check that ALL_NOW is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_NOW);
}

ZTEST_USER(cros_flash, test_protect_now)
{
	cros_flash_init(cros_flash_dev);

	/* Protect RO now. */
	cros_flash_physical_protect_now(cros_flash_dev, 0);

	/* Check that only option bytes are disabled. */
	zassert_equal(flash_block_protection_changes_fake.call_count, 1);
	zassert_equal(flash_block_control_access_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_protect_now_all)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	/* Check that only option bytes are disabled. */
	zassert_equal(flash_block_protection_changes_fake.call_count, 1);
	zassert_equal(flash_block_control_access_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_protect_at_boot_protection_changes_blocked)
{
	cros_flash_init(cros_flash_dev);

	/* Protect RO now. */
	cros_flash_physical_protect_now(cros_flash_dev, 0);

	/* Check that protect at boot fails */
	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      -EACCES);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_disabled_ro_at_boot)
{
	cros_flash_init(cros_flash_dev);

	cros_flash_physical_protect_at_boot(cros_flash_dev,
					    EC_FLASH_PROTECT_RO_AT_BOOT);

	/* When HW WP is disabled we expect only RDP to be enabled. */
	zassert_equal(flash_change_wp_fake.call_count, 0);
	zassert_equal(flash_change_rdp_fake.call_count, 1);
	zassert_true(rdp_enabled);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_enabled_ro_at_boot)
{
	cros_flash_init(cros_flash_dev);

	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_physical_protect_at_boot(cros_flash_dev,
					    EC_FLASH_PROTECT_RO_AT_BOOT);

	/* WP and RDP should be enabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffff);
	zassert_equal(flash_change_rdp_fake.call_count, 1);
	zassert_true(rdp_enabled);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_enabled_ro_at_boot_rdp_failed)
{
	cros_flash_init(cros_flash_dev);

	/* Enable HW WP. */
	gpio_wp_l_set(0);

	flash_change_rdp_fake.custom_fake = NULL;
	flash_change_rdp_fake.return_val = -EINVAL;

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      -EINVAL);

	/* WP should be enabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffff);
	zassert_equal(flash_change_rdp_fake.call_count, 1);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_enabled_ro_at_boot_wp_failed)
{
	cros_flash_init(cros_flash_dev);

	/* Enable HW WP. */
	gpio_wp_l_set(0);

	flash_change_wp_fake.custom_fake = NULL;
	flash_change_wp_fake.return_val = -EINVAL;

	zassert_equal(cros_flash_physical_protect_at_boot(
			      cros_flash_dev, EC_FLASH_PROTECT_RO_AT_BOOT),
		      -EINVAL);

	/* RDP should be enabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(flash_change_rdp_fake.call_count, 1);
	zassert_true(rdp_enabled);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_enabled_all_at_boot)
{
	cros_flash_init(cros_flash_dev);

	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_physical_protect_at_boot(cros_flash_dev,
					    EC_FLASH_PROTECT_ALL_AT_BOOT);

	/* Whole flash should be protected, but RDP disabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffffffff);
	zassert_equal(flash_change_rdp_fake.call_count, 0);
}

ZTEST_USER(cros_flash, test_protect_at_boot_hwwp_enabled_ro_all_at_boot)
{
	cros_flash_init(cros_flash_dev);

	/* Enable HW WP. */
	gpio_wp_l_set(0);

	cros_flash_physical_protect_at_boot(
		cros_flash_dev,
		EC_FLASH_PROTECT_ALL_AT_BOOT | EC_FLASH_PROTECT_RO_AT_BOOT);

	/* Whole flash should be protected, but RDP disabled. */
	zassert_equal(flash_change_wp_fake.call_count, 1);
	zassert_equal(protected_mask, 0xffffffff);
	zassert_equal(flash_change_rdp_fake.call_count, 1);
	zassert_true(rdp_enabled);
}

ZTEST_USER(cros_flash, test_get_protect_flags)
{
	cros_flash_init(cros_flash_dev);

	/* Check that nothing is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev), 0);
}

ZTEST_USER(cros_flash, test_get_protect_flags_control_disabled)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	/* Check that ALL_NOW is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_NOW);
}

ZTEST_USER(cros_flash, test_get_protect_flags_rdp_enabled)
{
	cros_flash_init(cros_flash_dev);

	/* Enable RDP. */
	rdp_enabled = true;

	/* Check that RO_AT_BOOT is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_RO_AT_BOOT);
}

ZTEST_USER(cros_flash, test_get_protect_flags_rdp_enabled_control_disabled)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	/* Enable RDP. */
	rdp_enabled = true;

	/* Check that RO_AT_BOOT is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ALL_NOW | EC_FLASH_PROTECT_RO_AT_BOOT);
}

ZTEST_USER(cros_flash, test_get_protect_flags_failed_to_get_rdp)
{
	cros_flash_init(cros_flash_dev);

	flash_get_rdp_fake.return_val = -ENOTSUP;
	flash_get_rdp_fake.custom_fake = NULL;

	/* Check that error is reported. */
	zassert_equal(cros_flash_physical_get_protect_flags(cros_flash_dev),
		      EC_FLASH_PROTECT_ERROR_UNKNOWN);
}

ZTEST_USER(cros_flash, test_get_protect)
{
	cros_flash_init(cros_flash_dev);

	protected_mask = 0x10001;

	zassert_true(cros_flash_physical_get_protect(cros_flash_dev, 0));
	zassert_true(cros_flash_physical_get_protect(cros_flash_dev, 16));
	zassert_false(cros_flash_physical_get_protect(cros_flash_dev, 15));
}

ZTEST_USER(cros_flash, test_get_protect_control_disabled)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	/* Check that protection is enabled inside and outside WP region. */
	zassert_true(cros_flash_physical_get_protect(cros_flash_dev, 0));
	zassert_true(cros_flash_physical_get_protect(cros_flash_dev, 31));
}

ZTEST_USER(cros_flash, test_get_protect_failure)
{
	cros_flash_init(cros_flash_dev);

	flash_get_wp_fake.return_val = -EINVAL;
	flash_get_wp_fake.custom_fake = NULL;

	/* Check that protection is disabled on sectors. */
	zassert_false(cros_flash_physical_get_protect(cros_flash_dev, 0));
	zassert_false(cros_flash_physical_get_protect(cros_flash_dev, 31));
}

ZTEST_USER(cros_flash, test_write)
{
	cros_flash_init(cros_flash_dev);

	uint32_t some_value = 42;

	cros_flash_physical_write(cros_flash_dev, 0x4000, sizeof(some_value),
				  (const char *)&some_value);

	uint32_t read_value = 0;

	crec_flash_physical_read(0x4000, sizeof(read_value),
				 (char *)&read_value);

	zassert_equal(some_value, read_value);
}

ZTEST_USER(cros_flash, test_erase)
{
	cros_flash_init(cros_flash_dev);

	uint32_t some_value = 42;

	cros_flash_physical_write(cros_flash_dev, 0x4000, sizeof(some_value),
				  (const char *)&some_value);

	cros_flash_physical_erase(cros_flash_dev, 0x4000,
				  DT_PROP(DT_CHOSEN(cros_ec_flash),
					  erase_block_size));

	uint32_t read_value = 0;

	crec_flash_physical_read(0x4000, sizeof(read_value),
				 (char *)&read_value);

	zassert_not_equal(some_value, read_value);
}

ZTEST_USER(cros_flash, test_write_control_disabled)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	uint32_t some_value = 42;

	zassert_equal(cros_flash_physical_write(cros_flash_dev, 0x4000,
						sizeof(some_value),
						(const char *)&some_value),
		      -EACCES);
}

ZTEST_USER(cros_flash, test_erase_control_disabled)
{
	cros_flash_init(cros_flash_dev);

	/* Protect ALL now. */
	cros_flash_physical_protect_now(cros_flash_dev, 1);

	zassert_equal(
		cros_flash_physical_erase(cros_flash_dev, 0x4000,
					  DT_PROP(DT_CHOSEN(cros_ec_flash),
						  erase_block_size)),
		-EACCES);
}

void cros_flash_before(void *fixture)
{
	/* Disable HW WP. */
	gpio_wp_l_set(1);

	protected_mask = 0x0;
	RESET_FAKE(flash_change_wp);
	RESET_FAKE(flash_get_wp);

	rdp_enabled = false;
	RESET_FAKE(flash_change_rdp);
	RESET_FAKE(flash_get_rdp);

	protection_changes_blocked = false;
	control_access_blocked = false;
	RESET_FAKE(flash_block_protection_changes);
	RESET_FAKE(flash_block_control_access);

	RESET_FAKE(system_reset);

	current_protection.control_access_blocked = false;
	current_protection.protection_changes_blocked = false;
	protection_struct_version = 1;
	RESET_FAKE(system_get_jump_tag)

	system_clear_reset_flags(0xffffffff);

	flash_get_wp_fake.custom_fake = flash_get_wp_custom_fake;
	flash_change_wp_fake.custom_fake = flash_change_wp_custom_fake;
	flash_get_rdp_fake.custom_fake = flash_get_rdp_custom_fake;
	flash_change_rdp_fake.custom_fake = flash_change_rdp_custom_fake;
	flash_block_protection_changes_fake.custom_fake =
		flash_block_protection_changes_custom_fake;
	flash_block_control_access_fake.custom_fake =
		flash_block_control_access_custom_fake;
	system_get_jump_tag_fake.custom_fake = system_get_jump_tag_custom_fake;
}

ZTEST_SUITE(cros_flash, drivers_predicate_post_main, NULL, cros_flash_before,
	    NULL, NULL);
