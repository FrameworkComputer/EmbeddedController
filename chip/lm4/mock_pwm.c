/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock PWM control module for Chrome EC */

#include "pwm.h"
#include "timer.h"
#include "uart.h"

static int fan_target_rpm;
static int kblight;

int pwm_set_fan_target_rpm(int rpm)
{
	uart_printf("Fan RPM: %d\n", rpm);
	fan_target_rpm = rpm;
	return EC_SUCCESS;
}


int pwm_get_fan_target_rpm(void)
{
	return fan_target_rpm;
}


int pwm_set_keyboard_backlight(int percent)
{
	uart_printf("KBLight: %d\n", percent);
	kblight = percent;
	return EC_SUCCESS;
}


int pwm_get_keyboard_backlight(void)
{
	return kblight;
}


int pwm_get_keyboard_backlight_enabled(void)
{
	/* Always enabled */
	return 1;
}


void pwm_task(void)
{
	/* Do nothing */
	while (1)
		usleep(5000000);
}
