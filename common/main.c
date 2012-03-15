/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "adc.h"
#include "charger.h"
#include "chip_temp_sensor.h"
#include "clock.h"
#include "config.h"
#include "console.h"
#include "eeprom.h"
#include "flash.h"
#include "gpio.h"
#include "i2c.h"
#include "jtag.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "memory_commands.h"
#include "peci.h"
#include "port80.h"
#include "power_button.h"
#include "powerdemo.h"
#include "pwm.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "tmp006.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "vboot.h"
#include "watchdog.h"

int main(void)
{
	/* Configure the pin multiplexers */
	configure_board();
	jtag_pre_init();

	/* Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator. */
	system_pre_init();

	/* Set the CPU clocks / PLLs and timer */
	clock_init();
	timer_init();
	/* The timer used by get_time() is now started, so everything after
	 * this can be benchmarked. */

	/* Do system, gpio, and vboot pre-initialization so we can jump to
	 * another image if necessary.  This must be done as early as
	 * possible, so that the minimum number of components get
	 * re-initialized if we jump to another image. */
	gpio_pre_init();
	vboot_pre_init();

	task_init();

#ifdef CONFIG_TASK_WATCHDOG
	watchdog_init(1100);
#endif
	uart_init();
	system_init();
#ifdef CONFIG_TASK_KEYSCAN
	keyboard_scan_init();
#endif
#ifdef CONFIG_FLASH
	flash_init();
#endif
	eeprom_init();

	vboot_init();

#ifdef CONFIG_LPC
	port_80_init();
	lpc_init();
	uart_comx_enable();
#endif
#ifdef CONFIG_PWM
	pwm_init();
#endif
	i2c_init();
#ifdef CONFIG_TEMP_SENSOR
	temp_sensor_init();
	chip_temp_sensor_init();
#endif
#ifdef CONFIG_TASK_POWERBTN
	power_button_init();
#endif
	adc_init();
	usb_charge_init();
#ifdef CONFIG_CHARGER
	charger_init();
#endif

#ifdef CONFIG_PECI
	peci_init();
#endif

	/* Print the init time and reset cause.  Init time isn't completely
	 * accurate because it can't take into account the time for the first
	 * few module inits, but it'll at least catch the majority of them. */
	uart_printf("\n\n--- Chrome EC initialized in %d us ---\n",
		    get_time().le.lo);
	uart_printf("build: %s\n", system_get_build_info());
	uart_printf("(image: %s, last reset: %s)\n",
		    system_get_image_copy_string(),
		    system_get_reset_cause_string());

	/* Launch task scheduling (never returns) */
	return task_start();
}
