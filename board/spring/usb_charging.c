/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control for spring board */

#include "board.h"
#include "console.h"
#include "registers.h"
#include "util.h"

#define PWM_FREQUENCY 100 /* Hz */

void board_configure_pwm(void)
{
	uint32_t val;

	/* Config alt. function (TIM3/PWM) */
	val = STM32_GPIO_CRL_OFF(GPIO_B) & ~0x000f0000;
	val |= 0x00090000;
	STM32_GPIO_CRL_OFF(GPIO_B) = val;

	/* Enable TIM3 clock */
	STM32_RCC_APB1ENR |= 0x2;

	/* Disable counter during setup */
	STM32_TIM_CR1(3) = 0x0000;

	/*
	 * CPU_CLOCK / PSC determines how fast the counter operates.
	 * ARR determines the wave period, CCRn determines duty cycle.
	 * Thus, frequency = CPU_CLOCK / PSC / ARR.
	 *
	 * Assuming 16MHz clock and ARR=100, PSC needed to achieve PWM_FREQUENCY
	 * is: PSC = CPU_CLOCK / PWM_FREQUENCY / ARR
	 */
	STM32_TIM_PSC(3) = CPU_CLOCK / PWM_FREQUENCY / 100; /* pre-scaler */
	STM32_TIM_ARR(3) = 100;			/* auto-reload value */
	STM32_TIM_CCR1(3) = 100;		/* duty cycle */

	/* CC1 configured as output, PWM mode 1, preload enable */
	STM32_TIM_CCMR1(3) = (6 << 4) | (1 << 3);

	/* CC1 output enable, active high */
	STM32_TIM_CCER(3) = (1 << 0);

	/* Generate update event to force loading of shadow registers */
	STM32_TIM_EGR(3) |= 1;

	/* Enable auto-reload preload, start counting */
	STM32_TIM_CR1(3) |= (1 << 7) | (1 << 0);
}

void board_pwm_duty_cycle(int percent)
{
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	STM32_TIM_CCR1(3) = (percent * STM32_TIM_ARR(3)) / 100;
}

/*
 * Console command for debugging.
 * TODO(victoryang): Remove after charging control is done.
 */
static int command_ilim(int argc, char **argv)
{
	char *e;
	int percent;

	if (argc >= 2) {
		percent = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		board_pwm_duty_cycle(percent);
	}
	ccprintf("PWM duty cycle set to %d%%\n", STM32_TIM_CCR1(3));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ilim, command_ilim,
		"[percent]",
		"Set or show ILIM duty cycle",
		NULL);
