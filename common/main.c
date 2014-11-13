/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "board_config.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "dma.h"
#include "eeprom.h"
#include "eoption.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "jtag.h"
#include "keyboard_scan.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "rsa.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

test_mockable int main(void)
{
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

#ifdef CONFIG_MPU
	mpu_pre_init();
#endif

	/* Configure the pin multiplexers and GPIOs */
	jtag_pre_init();
	gpio_pre_init();

#ifdef CONFIG_BOARD_POST_GPIO_INIT
	board_config_post_gpio_init();
#endif
	/*
	 * Initialize interrupts, but don't enable any of them.  Note that
	 * task scheduling is not enabled until task_start() below.
	 */
	task_pre_init();

	/*
	 * Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator.
	 */
	system_pre_init();
	system_common_pre_init();

#ifdef CONFIG_FLASH
	/*
	 * Initialize flash and apply write protect if necessary.  Requires
	 * the reset flags calculated by system initialization.
	 */
	flash_pre_init();
#endif

	/* Set the CPU clocks / PLLs.  System is now running at full speed. */
	clock_init();

	/*
	 * Initialize timer.  Everything after this can be benchmarked.
	 * get_time() and udelay() may now be used.  usleep() requires task
	 * scheduling, so cannot be used yet.  Note that interrupts declared
	 * via DECLARE_IRQ() call timer routines when profiling is enabled, so
	 * timer init() must be before uart_init().
	 */
	timer_init();

	/* Main initialization stage.  Modules may enable interrupts here. */
	cpu_init();

#ifdef CONFIG_DMA
	/* Initialize DMA.  Must be before UART. */
	dma_init();
#endif

	/* Initialize UART.  Console output functions may now be used. */
	uart_init();

	if (system_jumped_to_this_image()) {
		CPRINTS("UART initialized after sysjump");
	} else {
		CPUTS("\n\n--- UART initialized after reboot ---\n");
		CPUTS("[Reset cause: ");
		system_print_reset_flags();
		CPUTS("]\n");
	}
	CPRINTF("[Image: %s, %s]\n",
		 system_get_image_copy_string(), system_get_build_info());

#ifdef CONFIG_BRINGUP
	ccprintf("\n\nWARNING: BRINGUP BUILD\n\n\n");
#endif

#ifdef CONFIG_WATCHDOG
	/*
	 * Intialize watchdog timer.  All lengthy operations between now and
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
#ifdef CONFIG_EOPTION
	eoption_init();
#endif
#ifdef HAS_TASK_KEYSCAN
	keyboard_scan_init();
#endif

#ifdef CONFIG_RWSIG
	/*
	 * Check the RW firmware signature
	 * and eventually jump to it if it is good.
	 */
	check_rw_signature();
#endif

	/* Initialize the hook library.  This calls HOOK_INIT hooks. */
	hook_init();

	/*
	 * Print the init time.  Not completely accurate because it can't take
	 * into account the time before timer_init(), but it'll at least catch
	 * the majority of the time.
	 */
	CPRINTS("Inits done");

	/* Launch task scheduling (never returns) */
	return task_start();
}
