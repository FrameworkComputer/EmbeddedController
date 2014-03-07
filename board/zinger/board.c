/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Tiny charger configuration */

#include "common.h"
#include "debug.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

static void clock_init(void)
{
	/* put 1 Wait-State for flash access to ensure proper reads at 48Mhz */
	STM32_FLASH_ACR = 0x1001; /* 1 WS / Prefetch enabled */

	/* Ensure that HSI8 is ON */
	if (!(STM32_RCC_CR & (1 << 1))) {
		/* Enable HSI */
		STM32_RCC_CR |= 1 << 0;
		/* Wait for HSI to be ready */
		while (!(STM32_RCC_CR & (1 << 1)))
			;
	}
	/* PLLSRC = HSI, PLLMUL = x12 (x HSI/2) = 48Mhz */
	STM32_RCC_CFGR = 0x00288000;
	/* Enable PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for PLL to be ready */
	while (!(STM32_RCC_CR & (1 << 25)))
			;

	/* switch SYSCLK to PLL */
	STM32_RCC_CFGR = 0x00288002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

static void power_init(void)
{
	/* enable SYSCFG, COMP, ADC, SPI1, USART1, TIM17 */
	STM32_RCC_APB2ENR = 0x00045201;
	/* enable TIM2, TIM14, PWR */
	STM32_RCC_APB1ENR = 0x10000101;
	/* enable DMA, SRAM, CRC, GPA, GPB, GPC, GPF */
	STM32_RCC_AHBENR  = 0x4e0045;
	/* TODO: remove GPC on real board */
}

/* GPIO setting helpers */
#define OUT(n) (1 << ((n) * 2))
#define AF(n) (2 << ((n) * 2))
#define ANALOG(n) (3 << ((n) * 2))
#define HIGH(n) (1 << (n))
#define ODR(n) (1 << (n))
#define HISPEED(n) (3 << ((n) * 2))
#define AFx(n, x) (x << (((n) % 8) * 4))
/* GPIO level setting helpers through BSRR register */
#define GPIO_SET(n)   (1 << (n))
#define GPIO_RESET(n) (1 << ((n) + 16))

static void pins_init(void)
{
	/* Pin usage:
	 * PA0  ()                 : Wakeup on Vnc / Threshold
	 * PA1  (ANALOG - ADC_IN1) : CC sense
	 * PA2  (ANALOG - ADC_IN2) : Current sense
	 * PA3  (ANALOG - ADC_IN3) : Voltage sense
	 * PA4  (OUT - OD GPIO)    : PD TX enable
	 * PA5  (AF0 - SPI1_SCK)   : TX clock in
	 * PA6  (AF0 - SPI1_MISO)  : PD TX
	 * PA7  (AF5 - TIM17_CH1)  : PD RX
	 * PA9  (AF1 - UART1_TX)   : [DEBUG] UART TX
	 * PA10 (AF1 - UART1_RX)   : [DEBUG] UART RX
	 * PA13 (OUT - GPIO)       : voltage select[0]
	 * PA14 (OUT - GPIO)       : voltage select[1]
	 * PB1  (AF0 - TIM14_CH1)  : TX clock out
	 * PF0  (OUT - GPIO)       : LM5050 FET driver off
	 * PF1  (OUT - GPIO)       : discharge FET
	 */
	STM32_GPIO_ODR(GPIO_A) = HIGH(4) | HIGH(6);
	STM32_GPIO_AFRL(GPIO_A) = AFx(7, 5);
	STM32_GPIO_AFRH(GPIO_A) = AFx(9, 1) | AFx(10, 1);
	STM32_GPIO_OTYPER(GPIO_A) = ODR(4) | ODR(6);
	STM32_GPIO_OSPEEDR(GPIO_A) = HISPEED(5) | HISPEED(6) | HISPEED(7);
	STM32_GPIO_MODER(GPIO_A) = ANALOG(1) | ANALOG(2) | ANALOG(3) | OUT(4)
				 | AF(5) /*| AF(6)*/ | AF(7) | AF(9) | AF(10)
				 | OUT(13) | OUT(14);
	/* set PF0 / PF1 as output, PF0 is open-drain, high by default */
	STM32_GPIO_ODR(GPIO_F) = HIGH(0);
	STM32_GPIO_MODER(GPIO_F) = OUT(0) | OUT(1);
	STM32_GPIO_OTYPER(GPIO_F) = ODR(1);

	/* Set PB1 as AF0 (TIM14_CH1) */
	STM32_GPIO_OSPEEDR(GPIO_B) = HISPEED(1);
	STM32_GPIO_MODER(GPIO_B) = AF(1);

	/* --- dev board only --- */
	/*
	 * Blue LED    : PC8 (OUT)
	 * Green LED   : PC9 (OUT)
	 */
	STM32_GPIO_ODR(GPIO_C) = HIGH(9);
	STM32_GPIO_MODER(GPIO_C) = OUT(8) | OUT(9);
}


static void uart_init(void)
{
	/* set baudrate */
	STM32_USART_BRR(UARTN) =
		DIV_ROUND_NEAREST(CPU_CLOCK, CONFIG_UART_BAUD_RATE);
	/* UART enabled, 8 Data bits, oversampling x16, no parity */
	STM32_USART_CR1(UARTN) =
		STM32_USART_CR1_UE | STM32_USART_CR1_TE | STM32_USART_CR1_RE;
	/* 1 stop bit, no fancy stuff */
	STM32_USART_CR2(UARTN) = 0x0000;
	/* DMA disabled, special modes disabled, error interrupt disabled */
	STM32_USART_CR3(UARTN) = 0x0000;
}

static void timers_init(void)
{
	/* TIM2 is a 32-bit free running counter with 1Mhz frequency */
	STM32_TIM_CR2(2) = 0x0000;
	STM32_TIM32_ARR(2) = 0xFFFFFFFF;
	STM32_TIM32_CNT(2) = 0;
	STM32_TIM_PSC(2) = CPU_CLOCK / 1000000 - 1;
	STM32_TIM_EGR(2) = 0x0001; /* Reload the pre-scaler */
	STM32_TIM_CR1(2) = 1;
}

timestamp_t get_time(void)
{
	timestamp_t t;

	t.le.lo = STM32_TIM32_CNT(2);
	t.le.hi = 0;
	return t;
}

void udelay(unsigned us)
{
	unsigned t0 = STM32_TIM32_CNT(2);
	while ((STM32_TIM32_CNT(2) - t0) < us)
		;
}

static void hardware_init(void)
{
	power_init();
	clock_init();
	pins_init();
	uart_init();
	timers_init();
}

/* ------------------------- Power supply control ------------------------ */

/* Output voltage selection */
enum volt {
	VO_5V  = GPIO_RESET(13) | GPIO_RESET(14),
	VO_12V = GPIO_SET(13)   | GPIO_RESET(14),
	VO_13V = GPIO_RESET(13) | GPIO_SET(14),
	VO_20V = GPIO_SET(13)   | GPIO_SET(14),
};

static inline void set_output_voltage(enum volt v)
{
	/* set voltage_select on PA13/PA14 */
	STM32_GPIO_BSRR(GPIO_A) = v;
}

static inline void output_enable(void)
{
	/* GPF0 (FET driver shutdown) = 0 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_RESET(0);
}

static inline void output_disable(void)
{
	/* GPF0 (FET driver shutdown) = 1 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_SET(0);
}

/* default forced output voltage */
#define VO_DEFAULT VO_12V

int main(void)
{
	hardware_init();
	debug_printf("Power supply started ...\n");

	set_output_voltage(VO_DEFAULT);
	debug_printf("set output voltage : " STRINGIFY(VO_DEFAULT) "\n");
	output_enable();

	while (1) {
		/* magic LED blinker on the test board */
		STM32_GPIO_BSRR(GPIO_C) = GPIO_SET(8);
		udelay(200000);
		STM32_GPIO_BSRR(GPIO_C) = GPIO_RESET(8);
		udelay(750000);
		debug_printf("%T ALIVE\n");
	}
	while (1)
		;
}
