/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "pwm.h"
#include "registers.h"
#include "uart.h"
#include "util.h"

/* Maximum RPM for fan controller */
#define MAX_RPM 0x1fff
/* Max PWM for fan controller */
#define MAX_PWM 0x1ff


/* Configures the GPIOs for the fan module. */
static void configure_gpios(void)
{
#ifdef BOARD_link
	/* PK6 alternate function 1 = channel 1 PWM */
	gpio_set_alternate_function(LM4_GPIO_K, 0x40, 1);
	/* PM6:7 alternate function 1 = channel 0 PWM/tach */
	gpio_set_alternate_function(LM4_GPIO_M, 0xc0, 1);
#else
	/* PK6 alternate function 1 = channel 1 PWM */
	gpio_set_alternate_function(LM4_GPIO_K, 0x40, 1);
	/* PN6:7 alternate function 1 = channel 4 PWM/tach */
	gpio_set_alternate_function(LM4_GPIO_N, 0xc0, 1);
#endif
}


int pwm_get_fan_rpm(void)
{
	return LM4_FAN_FANCST(FAN_CH_CPU) & MAX_RPM;
}


int pwm_set_fan_target_rpm(int rpm)
{
	/* Treat out-of-range requests as requests for maximum fan speed */
	if (rpm < 0 || rpm > MAX_RPM)
		rpm = MAX_RPM;

	LM4_FAN_FANCMD(FAN_CH_CPU) = rpm;
	return EC_SUCCESS;
}


int pwm_set_keyboard_backlight(int percent)
{
	LM4_FAN_FANCMD(FAN_CH_KBLIGHT) = ((percent * MAX_PWM) / 100) << 16;
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Console commands */

static int command_fan_info(int argc, char **argv)
{
	uart_printf("Fan actual speed: %4d rpm\n", pwm_get_fan_rpm());
	uart_printf("    target speed: %4d rpm\n",
		    LM4_FAN_FANCMD(FAN_CH_CPU) & MAX_RPM);
	uart_printf("    duty cycle:   %d%%\n",
		    ((LM4_FAN_FANCMD(FAN_CH_CPU) >> 16)) * 100 / MAX_PWM);
	uart_printf("    status:       %d\n",
		    (LM4_FAN_FANSTS >> (2 * FAN_CH_CPU)) & 0x03);
	return EC_SUCCESS;
}


static int command_fan_set(int argc, char **argv)
{
	int rpm = 0;
	char *e;
	int rv;

	if (argc < 2) {
		uart_puts("Usage: fanset <rpm>\n");
		return EC_ERROR_UNKNOWN;
	}

	rpm = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid speed\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("Setting fan speed to %d rpm...\n", rpm);

        /* Move the fan to automatic control */
        if (LM4_FAN_FANCH(FAN_CH_CPU) & 0x0001) {
		LM4_FAN_FANCTL &= ~(1 << FAN_CH_CPU);
          LM4_FAN_FANCH(FAN_CH_CPU) &= ~0x0001;
          LM4_FAN_FANCTL |= (1 << FAN_CH_CPU);
        }

	rv = pwm_set_fan_target_rpm(rpm);
	if (rv == EC_SUCCESS)
		uart_printf("Done.\n");
	return rv;
}


/* TODO: this is a temporary command for debugging tach issues */
static int command_fan_duty(int argc, char **argv)
{
  int d = 0, pwm;
	char *e;

	if (argc < 2) {
		uart_puts("Usage: fanduty <percent>\n");
		return EC_ERROR_UNKNOWN;
	}

	d = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid duty cycle\n");
		return EC_ERROR_UNKNOWN;
	}

        pwm = (MAX_PWM * d) / 100;
	uart_printf("Setting fan duty cycle to %d%% = 0x%x...\n", d, pwm);

        /* Move the fan to manual control */
        if (!(LM4_FAN_FANCH(FAN_CH_CPU) & 0x0001)) {
		LM4_FAN_FANCTL &= ~(1 << FAN_CH_CPU);
		LM4_FAN_FANCH(FAN_CH_CPU) |= 0x0001;
		LM4_FAN_FANCTL |= (1 << FAN_CH_CPU);
        }

        /* Set the duty cycle */
	LM4_FAN_FANCMD(FAN_CH_CPU) = pwm << 16;

	return EC_SUCCESS;
}


static int command_kblight(int argc, char **argv)
{
	char *e;
	int rv;
	int i;

	if (argc < 2) {
		uart_puts("Usage: kblight <percent>\n");
		return EC_ERROR_UNKNOWN;
	}

	i = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid percent\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("Setting keyboard backlight to %d%%...\n", i);
	rv = pwm_set_keyboard_backlight(i);
	if (rv == EC_SUCCESS)
		uart_printf("Done.\n");
	return rv;
}


static const struct console_command console_commands[] = {
	{"fanduty", command_fan_duty},
	{"faninfo", command_fan_info},
	{"fanset", command_fan_set},
	{"kblight", command_kblight},
};
static const struct console_group command_group = {
	"PWM", console_commands, ARRAY_SIZE(console_commands)
};


/*****************************************************************************/
/* Initialization */

int pwm_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable the fan module and delay a few clocks */
	LM4_SYSTEM_RCGCFAN = 1;
	scratch = LM4_SYSTEM_RCGCFAN;

	/* Configure GPIOs */
	configure_gpios();

	/* Disable all fans */
	LM4_FAN_FANCTL = 0;

	/* Configure CPU fan:
	 * 0x8000 = bit 15     = auto-restart
	 * 0x0000 = bit 14     = slow acceleration
	 * 0x0000 = bits 13:11 = no hysteresis
	 * 0x0000 = bits 10:8  = start period (2<<0) edges
	 * 0x0000 = bits 7:6   = no fast start
	 * 0x0020 = bits 5:4   = average 4 edges when calculating RPM
	 * 0x0008 = bits 3:2   = 4 pulses per revolution
	 * 0x0000 = bit 0      = automatic control */
	LM4_FAN_FANCH(FAN_CH_CPU) = 0x8028;

	/* Configure keyboard backlight:
	 * 0x0000 = bit 15     = auto-restart
	 * 0x0000 = bit 14     = slow acceleration
	 * 0x0000 = bits 13:11 = no hysteresis
	 * 0x0000 = bits 10:8  = start period (2<<0) edges
	 * 0x0000 = bits 7:6   = no fast start
	 * 0x0000 = bits 5:4   = average 4 edges when calculating RPM
	 * 0x0000 = bits 3:2   = 4 pulses per revolution
	 * 0x0001 = bit 0      = manual control */
	LM4_FAN_FANCH(FAN_CH_KBLIGHT) = 0x0001;

	/* Set initial fan speed to maximum, backlight off */
	pwm_set_fan_target_rpm(-1);
	pwm_set_keyboard_backlight(0);

	/* Enable CPU fan and keyboard backlight */
	LM4_FAN_FANCTL |= (1 << FAN_CH_CPU) | (1 << FAN_CH_KBLIGHT);

	console_register_commands(&command_group);
	return EC_SUCCESS;
}
