/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "bbram.h"
#include "common.h"
#include "console.h"
#include "cros_version.h"
#include "system.h"
#include "watchdog.h"

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_system.h>

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 30

/* 2 second delay for waiting the H1 reset */
#define WAIT_RESET_TIME                                     \
	(CONFIG_PLATFORM_EC_PREINIT_HW_CYCLES_PER_SEC * 2 / \
	 CONFIG_PLATFORM_EC_WAIT_RESET_CYCLES_PER_ITERATION)

LOG_MODULE_REGISTER(shim_system, LOG_LEVEL_ERR);

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);
static const struct device *sys_dev;

/* Map idx to a bbram offset/size, or return -1 on invalid idx */
static int bbram_lookup(enum system_bbram_idx idx, int *offset_out,
			int *size_out)
{
	switch (idx) {
#if BBRAM_HAS_REGION(pd0)
	case SYSTEM_BBRAM_IDX_PD0:
		*offset_out = BBRAM_REGION_OFFSET(pd0);
		*size_out = BBRAM_REGION_SIZE(pd0);
		break;
#endif
#if BBRAM_HAS_REGION(pd1)
	case SYSTEM_BBRAM_IDX_PD1:
		*offset_out = BBRAM_REGION_OFFSET(pd1);
		*size_out = BBRAM_REGION_SIZE(pd1);
		break;
#endif
#if BBRAM_HAS_REGION(pd2)
	case SYSTEM_BBRAM_IDX_PD2:
		*offset_out = BBRAM_REGION_OFFSET(pd2);
		*size_out = BBRAM_REGION_SIZE(pd2);
		break;
#endif
#if BBRAM_HAS_REGION(try_slot)
	case SYSTEM_BBRAM_IDX_TRY_SLOT:
		*offset_out = BBRAM_REGION_OFFSET(try_slot);
		*size_out = BBRAM_REGION_SIZE(try_slot);
		break;
#endif
	default:
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	int offset, size, rc;

	if (bbram_dev == NULL)
		return EC_ERROR_INVAL;

	rc = bbram_lookup(idx, &offset, &size);
	if (rc)
		return rc;

	rc = bbram_read(bbram_dev, offset, size, value);

	return rc ? EC_ERROR_INVAL : EC_SUCCESS;
}

void chip_save_reset_flags(uint32_t flags)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return;
	}

	bbram_write(bbram_dev, BBRAM_REGION_OFFSET(saved_reset_flags),
		    BBRAM_REGION_SIZE(saved_reset_flags), (uint8_t *)&flags);
}

uint32_t chip_read_reset_flags(void)
{
	uint32_t flags;

	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return 0;
	}

	bbram_read(bbram_dev, BBRAM_REGION_OFFSET(saved_reset_flags),
		   BBRAM_REGION_SIZE(saved_reset_flags), (uint8_t *)&flags);

	return flags;
}

int system_set_scratchpad(uint32_t value)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return -EC_ERROR_INVAL;
	}

	return bbram_write(bbram_dev, BBRAM_REGION_OFFSET(scratchpad),
			   BBRAM_REGION_SIZE(scratchpad), (uint8_t *)&value);
}

int system_get_scratchpad(uint32_t *value)
{
	if (bbram_dev == NULL) {
		LOG_ERR("bbram_dev doesn't binding");
		return -EC_ERROR_INVAL;
	}

	if (bbram_read(bbram_dev, BBRAM_REGION_OFFSET(scratchpad),
		       BBRAM_REGION_SIZE(scratchpad), (uint8_t *)value)) {
		return -EC_ERROR_INVAL;
	}

	return 0;
}

test_mockable void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	int err;

	/* Flush console before hibernating */
	cflush();

	if (board_hibernate)
		board_hibernate();

	/* Save 'wake-up from hibernate' reset flag */
	chip_save_reset_flags(chip_read_reset_flags() |
			      EC_RESET_FLAG_HIBERNATE);

	err = cros_system_hibernate(sys_dev, seconds, microseconds);
	if (err < 0) {
		LOG_ERR("hibernate failed %d", err);
		return;
	}

	/*
	 * Ignore infinite loop for coverage as the test would fail via timeout
	 * and not report regardless of executing code.
	 */
	/* LCOV_EXCL_START */
	/* should never reach this point */
	while (1)
		continue;
	/* LCOV_EXCL_STOP */
}

#ifdef CONFIG_PM
/**
 * Print low power idle statistics
 */
static int command_idle_stats(int argc, const char **argv)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");

	timestamp_t ts = get_time();
	uint64_t deep_sleep_ticks = cros_system_deep_sleep_ticks(sys_dev);

	ccprintf("Time spent in deep-sleep:            %.6llds\n",
		 k_ticks_to_us_near64(deep_sleep_ticks));
	ccprintf("Total time on:                       %.6llds\n", ts.val);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(idlestats, command_idle_stats, "",
			"Print last idle stats");
#endif

const char *system_get_chip_vendor(void)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");

	return cros_system_chip_vendor(sys_dev);
}

const char *system_get_chip_name(void)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");

	return cros_system_chip_name(sys_dev);
}

const char *system_get_chip_revision(void)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");

	return cros_system_chip_revision(sys_dev);
}

test_mockable void system_reset(int flags)
{
	int err;
	uint32_t save_flags;

	if (!sys_dev)
		LOG_ERR("sys_dev get binding failed");

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	/*  Get flags to be saved in BBRAM */
	system_encode_save_flags(flags, &save_flags);

	/* Store flags to battery backed RAM. */
	chip_save_reset_flags(save_flags);

	/* If WAIT_EXT is set, then allow 10 seconds for external reset */
	if (flags & SYSTEM_RESET_WAIT_EXT) {
		int i;

		/* Wait 10 seconds for external reset */
		for (i = 0; i < 1000; i++) {
			watchdog_reload();
			udelay(10000);
		}
	}

	err = cros_system_soc_reset(sys_dev);

	if (err < 0)
		LOG_ERR("soc reset failed");

	/*
	 * Ignore infinite loop for coverage as the test would fail via timeout
	 * and not report regardless of executing code.
	 */
	/* LCOV_EXCL_START */
	/* should never return */
	while (1)
		continue;
	/* LCOV_EXCL_STOP */
}

static int check_reset_cause(void)
{
	uint32_t chip_flags = 0; /* used to write back to the BBRAM */
	uint32_t system_flags = chip_read_reset_flags(); /* system reset flag */
	int chip_reset_cause = 0; /* chip-level reset cause */

	chip_reset_cause = cros_system_get_reset_cause(sys_dev);
	if (chip_reset_cause < 0)
		return -1;

	/*
	 * TODO(b/182876692): Implement CONFIG_POWER_BUTTON_INIT_IDLE &
	 * CONFIG_BOARD_FORCE_RESET_PIN.
	 */

	switch (chip_reset_cause) {
	case POWERUP:
		system_flags |= EC_RESET_FLAG_POWER_ON;
		/*
		 * Power-on restart, so set a flag and save it for the next
		 * imminent reset. Later code will check for this flag and wait
		 * for the second reset. Waking from PSL hibernate is power-on
		 * for EC but not for H1, so do not wait for the second reset.
		 */
		if (IS_ENABLED(CONFIG_BOARD_RESET_AFTER_POWER_ON) &&
		    ((system_flags & EC_RESET_FLAG_HIBERNATE) == 0)) {
			system_flags |= EC_RESET_FLAG_INITIAL_PWR;
			chip_flags |= EC_RESET_FLAG_INITIAL_PWR;
		}
		break;

	case VCC1_RST_PIN:
		/*
		 * If configured, check the saved flags to see whether the
		 * previous restart was a power-on, in which case treat this
		 * restart as a power-on as well. This is to workaround the fact
		 * that the H1 will reset the EC at power up.
		 */
		if (IS_ENABLED(CONFIG_BOARD_RESET_AFTER_POWER_ON)) {
			if (system_flags & EC_RESET_FLAG_INITIAL_PWR) {
				/*
				 * The previous restart was a power-on so treat
				 * this restart as that, and clear the flag so
				 * later code will not wait for the second
				 * reset.
				 */
				system_flags = (system_flags &
						~EC_RESET_FLAG_INITIAL_PWR) |
					       EC_RESET_FLAG_POWER_ON;
			} else {
				/*
				 * No previous reset flag, so this is a
				 * subsequent restart i.e any restarts after the
				 * second restart caused by the H1.
				 */
				system_flags |= EC_RESET_FLAG_RESET_PIN;
			}
		} else {
			system_flags |= EC_RESET_FLAG_RESET_PIN;
		}
		break;

	case DEBUG_RST:
		system_flags |= EC_RESET_FLAG_SOFT;
		break;

	case WATCHDOG_RST:
		/*
		 * Don't set EC_RESET_FLAG_WATCHDOG flag if watchdog is issued
		 * by system_reset or hibernate in order to distinguish reset
		 * cause is panic reason or not.
		 */
		if (!(system_flags & (EC_RESET_FLAG_SOFT | EC_RESET_FLAG_HARD |
				      EC_RESET_FLAG_HIBERNATE)))
			system_flags |= EC_RESET_FLAG_WATCHDOG;
		break;
	}

	/* Clear & set the reset flags for the following reset. */
	chip_save_reset_flags(chip_flags);

	/* Set the system reset flags. */
	system_set_reset_flags(system_flags);

	return 0;
}

test_export_static int system_preinitialize(void)
{
	if (bbram_dev && !device_is_ready(bbram_dev)) {
		LOG_ERR("device %s not ready", bbram_dev->name);
		return -1;
	}

	sys_dev = device_get_binding("CROS_SYSTEM");
	if (!sys_dev) {
		/*
		 * TODO(b/183022804): This should not happen in normal
		 * operation. Check whether the error check can be change to
		 * build-time error, or at least a fatal run-time error.
		 */
		LOG_ERR("sys_dev gets binding failed");
		return -1;
	}

	/* check the reset cause */
	if (check_reset_cause() != 0) {
		LOG_ERR("check the reset cause failed");
		return -1;
	}

	/*
	 * For some boards on power-on, the EC is reset by the H1 after
	 * power-on, so the EC sees 2 resets. This config enables the EC to save
	 * a flag on the first power-up restart, and then wait for the second
	 * reset before any other setup is done (such as GPIOs, timers, UART
	 * etc.) On the second reset, the saved flag is used to detect the
	 * previous power-on, and treat the second reset as a power-on instead
	 * of a reset.
	 */
#ifdef CONFIG_BOARD_RESET_AFTER_POWER_ON
	if (system_get_reset_flags() & EC_RESET_FLAG_INITIAL_PWR) {
		/*
		 * The current initial stage couldn't use the kernel delay
		 * function. Use CPU nop instruction to wait for the external
		 * reset from H1.
		 */
		for (uint32_t i = WAIT_RESET_TIME; i; i--)
			arch_nop();
	}
#endif
	system_common_pre_init();
	return 0;
}

SYS_INIT(system_preinitialize, PRE_KERNEL_1,
	 CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY);
