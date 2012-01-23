/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Main routine for Chrome EC
 */

#include "registers.h"
#include "board.h"

#include "adc.h"
#include "clock.h"
#include "console.h"
#include "eeprom.h"
#include "flash.h"
#include "flash_commands.h"
#include "gpio.h"
#include "i2c.h"
#include "jtag.h"
#include "keyboard.h"
#include "lpc.h"
#include "memory_commands.h"
#include "port80.h"
#include "power_button.h"
#include "powerdemo.h"
#include "pwm.h"
#include "pwm_commands.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "vboot.h"
#include "watchdog.h"
#include "usb_charge.h"

/* example task blinking the user LED */
/* TODO: This also kicks the watchdog, so MUST be present! */
void UserLedBlink(void)
{
	while (1) {
#ifdef BOARD_bds
		gpio_set_level(GPIO_DEBUG_LED, 1);
#endif
		usleep(500000);
		watchdog_reload();
#ifdef BOARD_bds
		gpio_set_level(GPIO_DEBUG_LED, 0);
#endif
		usleep(500000);
		watchdog_reload();
	}
}


int main(void)
{
	/* Configure the pin multiplexers */
	configure_board();
	jtag_pre_init();

	/* Initialize the system module.  This enables the hibernate clock
	 * source we need to calibrate the internal oscillator. */
	system_pre_init();

	/* Set the CPU clocks / PLLs */
	clock_init();

	/* Do system, gpio, and vboot pre-initialization so we can jump to
	 * another image if necessary.  This must be done as early as
	 * possible, so that the minimum number of components get
	 * re-initialized if we jump to another image. */
	gpio_pre_init();
	vboot_pre_init();

	task_init();

	watchdog_init(1100);
	timer_init();
	uart_init();
	system_init();
	flash_init();
	eeprom_init();
	port_80_init();
	lpc_init();
	pwm_init();
	i2c_init();
	temp_sensor_init();
	power_button_init();
	adc_init();
	usb_charge_init();

	/* Print the reset cause */
	uart_printf("\n\n--- Chrome EC initialized! ---\n");
	uart_printf("(image: %s, version: %s, last reset: %s)\n",
		    system_get_image_copy_string(),
		    system_get_version(SYSTEM_IMAGE_UNKNOWN),
		    system_get_reset_cause_string());

	/* Launch task scheduling (never returns) */
	return task_start();
}
