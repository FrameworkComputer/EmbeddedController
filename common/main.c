/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "board_config.h"
#ifdef CONFIG_KEYBOARD_SCAN_ADC
#include "adc.h"
#endif
#include "button.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "cros_board_info.h"
#include "dma.h"
#include "eeprom.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "keyboard_scan.h"
#include "link_defs.h"
#include "lpc.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "panic.h"
#include "rwsig.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "vboot.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

test_mockable __keep int main(void)
{
	int mpu_pre_init_rv = EC_SUCCESS;

	if (IS_ENABLED(CONFIG_PRESERVE_LOGS)) {
		/*
		 * Initialize tx buffer head and tail. This needs to be done
		 * before any updates of uart tx input because we need to
		 * verify if the values remain the same after every EC reset.
		 */
		uart_init_buffer();

		/*
		 * Initialize reset logs. Needs to be done before any updates of
		 * reset logs because we need to verify if the values remain
		 * the same after every EC reset.
		 */
		if (IS_ENABLED(CONFIG_CMD_AP_RESET_LOG))
			init_reset_log();
	}

	/*
	 * Pre-initialization (pre-verified boot) stage.  Initialization at
	 * this level should do as little as possible, because verified boot
	 * may need to jump to another image, which will repeat this
	 * initialization.  In particular, modules should NOT enable
	 * interrupts.
	 */
#ifdef CONFIG_BOARD_PRE_INIT
	board_config_pre_init();
#endif

#ifdef CONFIG_CHIP_PRE_INIT
	chip_pre_init();
#endif

#ifdef CONFIG_MPU
	mpu_pre_init_rv = mpu_pre_init();
#endif

	gpio_pre_init();

#ifdef CONFIG_BOARD_POST_GPIO_INIT
	board_config_post_gpio_init();
#endif
	/*
	 * Initialize tasks, but don't enable any of them.  Note that
	 * task scheduling is not enabled until task_start() below.
	 */
	task_pre_init();

	/*
	 * Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator.
	 */
	system_pre_init();
	system_common_pre_init();

#ifdef CONFIG_DRAM_BASE
	/* Now that DRAM is initialized, clear up DRAM .bss, copy .data over. */
	memset(&__dram_bss_start, 0,
	       (uintptr_t)(&__dram_bss_end) - (uintptr_t)(&__dram_bss_start));
	memcpy(&__dram_data_start, &__dram_data_lma_start,
	       (uintptr_t)(&__dram_data_end) - (uintptr_t)(&__dram_data_start));
#endif

#if defined(CHIP_VARIANT_MT8195) && defined(CONFIG_CHIP_MEMORY_REGIONS)
	/* clear up NOLOAD region. */
#define REGION(name, attr, start, size) \
	memset(&__##name##_start, 0,    \
	       (uintptr_t)(&__##name##_end) - (uintptr_t)(&__##name##_start));
#include "memory_regions.inc"
#undef REGION
#endif

#if defined(CONFIG_FLASH_PHYSICAL)
	/*
	 * Initialize flash and apply write protect if necessary.  Requires
	 * the reset flags calculated by system initialization.
	 */
	crec_flash_pre_init();
#endif

	/* Set the CPU clocks / PLLs.  System is now running at full speed. */
	clock_init();

	/*
	 * Initialize timer.  Everything after this can be benchmarked.
	 * get_time() and udelay() may now be used.  crec_usleep() requires task
	 * scheduling, so cannot be used yet.  Note that interrupts declared
	 * via DECLARE_IRQ() call timer routines when profiling is enabled, so
	 * timer init() must be before uart_init().
	 */
	timer_init();

	/* Compensate the elapsed time for the RTC. */
	if (IS_ENABLED(CONFIG_HIBERNATE_PSL_COMPENSATE_RTC))
		system_compensate_rtc();

	/* Main initialization stage.  Modules may enable interrupts here. */
	cpu_init();

#ifdef CONFIG_DMA_CROS
	/* Initialize DMA.  Must be before UART. */
	dma_init();
#endif

	/* Initialize UART.  Console output functions may now be used. */
	uart_init();

	/* We wait to report the failure until here where we have console. */
	if (mpu_pre_init_rv != EC_SUCCESS)
		panic("MPU init failed");

	system_print_banner();

#ifdef CONFIG_BRINGUP
	ccprintf("\n\nWARNING: BRINGUP BUILD\n\n\n");
#endif

#ifdef CONFIG_WATCHDOG
	/*
	 * Initialize watchdog timer.  All lengthy operations between now and
	 * task_start() must periodically call watchdog_reload() to avoid
	 * triggering a watchdog reboot.  (This pretty much applies only to
	 * verified boot, because all *other* lengthy operations should be done
	 * by tasks.)
	 */
	watchdog_init();
#endif

	/*
	 * Verified boot needs to read the initial keyboard state and EEPROM
	 * contents.  EEPROM must be up first, so keyboard_scan can toggle
	 * debugging settings via keys held at boot.
	 */
#ifdef CONFIG_EEPROM
	eeprom_init();
#endif

	/*
	 * If the EC has exclusive control over the CBI EEPROM WP signal, have
	 * the EC set the WP if appropriate.  Note that once the WP is set, the
	 * EC must be reset via EC_RST_ODL in order for the WP to become unset.
	 */
	if (IS_ENABLED(CONFIG_EEPROM_CBI_WP) && system_is_locked())
		cbi_latch_eeprom_wp();

#ifdef CONFIG_HOSTCMD_X86
	/*
	 * Keyboard scan init/Button init can set recovery events to
	 * indicate to host entry into recovery mode. Before this is
	 * done, LPC_HOST_EVENT_ALWAYS_REPORT mask needs to be
	 * initialized correctly.
	 */
	lpc_init_mask();
#endif
	if (IS_ENABLED(CONFIG_I2C_CONTROLLER)) {
		/*
		 * Some devices (like the I2C keyboards, CBI) need I2C access
		 * pretty early, so let's initialize the controller now.
		 */
		i2c_init();

		if (IS_ENABLED(CONFIG_I2C_BITBANG)) {
			/*
			 * Enable I2C raw mode for the ports which need
			 * pre-task i2c transactions.
			 */
			enable_i2c_raw_mode(true);

			/* Board level pre-task I2C peripheral initialization */
			board_pre_task_i2c_peripheral_init();
		}
	}

	/*
	 * Copy this line in case you need even earlier hooks instead of moving
	 * it. Callbacks of this type are expected to handle multiple calls.
	 */
	hook_notify(HOOK_INIT_EARLY);

#ifdef HAS_TASK_KEYSCAN

#ifdef CONFIG_KEYBOARD_SCAN_ADC
	/*
	 * Initialize adc here as we need to use it during keyboard_scan_init
	 * to scan boot keys
	 */
	adc_init();
#endif

	keyboard_scan_init();
#endif /* HAS_TASK_KEYSCAN */

#if defined(CONFIG_DEDICATED_RECOVERY_BUTTON) || defined(CONFIG_VOLUME_BUTTONS)
	button_init();
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON | CONFIG_VOLUME_BUTTONS) */

	/* Make sure recovery boot won't be paused. */
	if (IS_ENABLED(CONFIG_POWER_BUTTON_INIT_IDLE) &&
	    system_is_manual_recovery() &&
	    (system_get_reset_flags() & EC_RESET_FLAG_AP_IDLE)) {
		CPRINTS("Clear AP_IDLE for recovery mode");
		system_clear_reset_flags(EC_RESET_FLAG_AP_IDLE);
	}

#if defined(CONFIG_VBOOT_EFS) || defined(CONFIG_VBOOT_EFS2)
	/*
	 * Execute PMIC reset in case we're here after watchdog reset to unwedge
	 * AP. This has to be done here because vboot_main may jump to RW.
	 */
	if (IS_ENABLED(CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET))
		chipset_handle_reboot();
	/*
	 * For RO, it behaves as follows:
	 *   In recovery, it enables PD communication and returns.
	 *   In normal boot, it verifies and jumps to RW.
	 * For RW, it returns immediately.
	 */
	vboot_main();
#elif defined(CONFIG_RWSIG) && !defined(HAS_TASK_RWSIG)
	/*
	 * Check the RW firmware signature and jump to it if it is good.
	 *
	 * Only the Read-Only firmware needs to do the signature check.
	 */
	if (system_get_image_copy() == EC_IMAGE_RO) {
#if defined(CONFIG_RWSIG_DONT_CHECK_ON_PIN_RESET)
		/*
		 * If system was reset by reset-pin, do not jump and wait for
		 * command from host
		 */
		if (system_get_reset_flags() == EC_RESET_FLAG_RESET_PIN)
			CPRINTS("Hard pin-reset detected, disable RW jump");
		else
#endif
		{
			if (rwsig_check_signature())
				rwsig_jump_now();
		}
	}
#endif /* !CONFIG_VBOOT_EFS && CONFIG_RWSIG && !HAS_TASK_RWSIG */

	/*
	 * Disable I2C raw mode for the ports which needed pre-task i2c
	 * transactions as the task is about to start and the I2C can resume
	 * to event based transactions.
	 */
	if (IS_ENABLED(CONFIG_I2C_BITBANG) && IS_ENABLED(CONFIG_I2C_CONTROLLER))
		enable_i2c_raw_mode(false);

	/*
	 * Print the init time.  Not completely accurate because it can't take
	 * into account the time before timer_init(), but it'll at least catch
	 * the majority of the time.
	 */
	CPRINTS("Inits done");

	/* Launch task scheduling (never returns) */
	return task_start();
}
