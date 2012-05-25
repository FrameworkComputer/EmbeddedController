/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "clock.h"
#include "config.h"
#include "eeprom.h"
#include "eoption.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "jtag.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "vboot.h"
#include "watchdog.h"

int main(void)
{
	/* Pre-initialization (pre-verified boot) stage.  Initialization at
	 * this level should do as little as possible, because verified boot
	 * may need to jump to another image, which will repeat this
	 * initialization.  In particular, modules should NOT enable
	 * interrupts.*/

	/* Configure the pin multiplexers and GPIOs */
	configure_board();
	jtag_pre_init();
	gpio_pre_init();

	/* Initialize interrupts, but don't enable any of them.  Note that
	 * task scheduling is not enabled until task_start() below. */
	task_pre_init();

#ifdef CONFIG_FLASH
	flash_pre_init();
#endif

#ifdef CONFIG_VBOOT
	/* Verified boot pre-init.  This write-protects flash if necessary.
	 * Flash and GPIOs must be initialized first. */
	vboot_pre_init();
#endif

	/* Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator. */
	system_pre_init();
	system_common_pre_init();

	/* Set the CPU clocks / PLLs.  System is now running at full speed. */
	clock_init();

	/* Initialize timer.  Everything after this can be benchmarked.
	 * get_time() and udelay() may now be used.  usleep() requires task
	 * scheduling, so cannot be used yet.  Note that interrupts declared
	 * via DECLARE_IRQ() call timer routines when profiling is enabled, so
	 * timer init() must be before uart_init(). */
	timer_init();

	/* Main initialization stage.  Modules may enable interrupts here. */

	/* Initialize UART.  uart_printf(), etc. may now be used. */
	uart_init();
	if (system_jumped_to_this_image())
		uart_printf("[%T UART initialized after sysjump]\n");
	else {
		uart_puts("\n\n--- UART initialized after reboot ---\n");
		uart_printf("[Reset cause: %s]\n",
			    system_get_reset_cause_string());
	}
	uart_printf("[Image: %s, %s]\n",
		    system_get_image_copy_string(),
		    system_get_build_info());


#ifdef CONFIG_TASK_WATCHDOG
	/* Intialize watchdog timer.  All lengthy operations between now and
	 * task_start() must periodically call watchdog_reload() to avoid
	 * triggering a watchdog reboot.  (This pretty much applies only to
	 * verified boot, because all *other* lengthy operations should be done
	 * by tasks.) */
	watchdog_init(1100);
#endif

	/* Verified boot needs to read the initial keyboard state and EEPROM
	 * contents.  EEPROM must be up first, so keyboard_scan can toggle
	 * debugging settings via keys held at boot. */
#ifdef CONFIG_EEPROM
	eeprom_init();
#endif
#ifdef CONFIG_EOPTION
	eoption_init();
#endif
#ifdef CONFIG_TASK_KEYSCAN
	keyboard_scan_init();
#endif

#ifdef CONFIG_VBOOT
	/* Verified boot initialization.  This may jump to another image, which
	 * will need to reconfigure / reinitialize the system, so as little as
	 * possible should be done above this step.
	 *
	 * Note that steps above here may be done TWICE per boot, once in the
	 * RO image and once in the RW image. */
	vboot_init();

	/* If system is locked, disable system jumps now that vboot has had its
	 * chance to jump to a RW image. */
	if (system_is_locked())
		system_disable_jump();
#endif

	/* Initialize other driver modules.  These can occur in any order.
	 * Non-driver modules with tasks do their inits from their task
	 * functions, not here. */
	hook_notify(HOOK_INIT, 0);

#ifdef BOARD_link
	/* Reduce core clock now that init is done */
	clock_enable_pll(0);
#endif
	/* Print the init time.  Not completely accurate because it can't take
	 * into account the time for the first few module inits, but it'll at
	 * least catch the majority of them. */
	uart_printf("[%T Inits done]\n");

	/* Launch task scheduling (never returns) */
	return task_start();
}
