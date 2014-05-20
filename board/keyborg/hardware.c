/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Hardware initialization and common functions */

#include "common.h"
#include "cpu.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "touch_scan.h"
#include "util.h"

static void clock_init(void)
{
	/* Turn on HSE */
	if (!(STM32_RCC_CR & (1 << 17))) {
		/* Enable HSE */
		STM32_RCC_CR |= (1 << 18) | (1 << 16);
		/* Wait for HSE to be ready */
		while (!(STM32_RCC_CR & (1 << 17)))
			;
	}

	/* PLLSRC = HSE/2 = 8MHz, PLLMUL = x6 = 48MHz */
	STM32_RCC_CFGR = 0x00534000;
	/* Enable PLL */
	STM32_RCC_CR |= 1 << 24;
	/* Wait for PLL to be ready */
	while (!(STM32_RCC_CR & (1 << 25)))
			;

	/* switch SYSCLK to PLL */
	STM32_RCC_CFGR = 0x00534002;
	/* wait until the PLL is the clock source */
	while ((STM32_RCC_CFGR & 0xc) != 0x8)
		;
}

static void power_init(void)
{
	/* enable ADC1, ADC2, PMSE, SPI1, GPA-GPI, AFIO */
	STM32_RCC_APB2ENR = 0x0000f7fd;
	/* enable TIM2, TIM3, PWR */
	STM32_RCC_APB1ENR = 0x10000003;
	/* enable DMA, SRAM */
	STM32_RCC_AHBENR  = 0x000005;
}

/* GPIO setting helpers */
#define OUT(n)    (0x1 << ((n & 0x7) * 4))
#define OUT50(n)  (0x3 << ((n & 0x7) * 4))
#define ANALOG(n) (0x0)
#define FLOAT(n)  (0x4 << ((n & 0x7) * 4))
#define GP_OD(n)  (0x5 << ((n & 0x7) * 4))
#define AF_PP(n)  (0x9 << ((n & 0x7) * 4))
#define AF_OD(n)  (0xd << ((n & 0x7) * 4))
#define LOW(n)    (1 << (n + 16))
#define HIGH(n)   (1 << n)
#define INT(n)    (1 << n)

static void pins_init(void)
{
	/*
	 * Disable JTAG and SWD. We want JTDI for UART Tx and SWD pins for
	 * touch scan.
	 */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x7 << 24))
			       | (4 << 24);

	/*
	 * Initial pin usage:
	 * PA0:  SPI_NSS  - INPUT/INT_FALLING
	 * PA1:  N_CHG    - INPUT
	 * PA3:  SPI_CLK  - INPUT
	 * PA4:  SPI_MISO - INPUT
	 * PA6:  CS1      - OUTPUT/HIGH
	 * PA7:  SPI_MOSI - INPUT
	 * PA9:  USB_PU   - OUTPUT/LOW
	 * PA15: UART TX  - OUTPUT/HIGH
	 * PI1:  SYNC1    - OUTPUT/LOW
	 * PI2:  SYNC2    - OUTPUT/LOW
	 */
	STM32_GPIO_CRL(GPIO_A) = FLOAT(0) | FLOAT(1) | FLOAT(3) | FLOAT(4) |
				 OUT(6) | FLOAT(7);
	STM32_GPIO_CRH(GPIO_A) = OUT(9) | OUT(15);
	STM32_GPIO_BSRR(GPIO_A) = LOW(1) | HIGH(6) | LOW(9) | HIGH(15);
	STM32_EXTI_FTSR |= INT(0);

	STM32_GPIO_CRL(GPIO_I) = OUT(1) | OUT(2);
	STM32_GPIO_BSRR(GPIO_I) = LOW(1) | LOW(2);
}

static void adc_init(void)
{
	int id;

	for (id = 0; id < 2; ++id) {
		/* Enable ADC clock */
		STM32_RCC_APB2ENR |= (1 << (14 + id));

		/* Power on ADC if it's off */
		if (!(STM32_ADC_CR2(id) & (1 << 0))) {
			/* Power on ADC module */
			STM32_ADC_CR2(id) |= (1 << 0);  /* ADON */

			/* Reset calibration */
			STM32_ADC_CR2(id) |= (1 << 3);  /* RSTCAL */
			while (STM32_ADC_CR2(id) & (1 << 3))
				;

			/* A/D Calibrate */
			STM32_ADC_CR2(id) |= (1 << 2);  /* CAL */
			while (STM32_ADC_CR2(id) & (1 << 2))
				;
		}

		/* Set right alignment */
		STM32_ADC_CR2(id) &= ~(1 << 11);

		/* Set sampling time */
		STM32_ADC_SMPR2(id) = ADC_SMPR_VAL;

		/* Select AIN0 */
		STM32_ADC_SQR3(id) &= ~0x1f;

		/* Disable DMA */
		STM32_ADC_CR2(id) &= ~(1 << 8);

		/* Disable scan mode */
		STM32_ADC_CR1(id) &= ~(1 << 8);
	}
}

static void timers_init(void)
{
	STM32_TIM_CR1(3) = 0x0004; /* MSB */
	STM32_TIM_CR1(2) = 0x0004; /* LSB */

	STM32_TIM_CR2(3) = 0x0000;
	STM32_TIM_CR2(2) = 0x0020;

	STM32_TIM_SMCR(3) = 0x0007 | (1 << 4);
	STM32_TIM_SMCR(2) = 0x0000;

	STM32_TIM_ARR(3) = 0xffff;
	STM32_TIM_ARR(2) = 0xffff;

	STM32_TIM_PSC(3) = 0;
	STM32_TIM_PSC(2) = CPU_CLOCK / 1000000 - 1;

	STM32_TIM_EGR(3) = 0x0001;
	STM32_TIM_EGR(2) = 0x0001;

	STM32_TIM_DIER(3) = 0x0001;
	STM32_TIM_DIER(2) = 0x0000;

	STM32_TIM_CR1(3) |= 1;
	STM32_TIM_CR1(2) |= 1;

	STM32_TIM_CNT(3) = 0;
	STM32_TIM_CNT(2) = 0;

	task_enable_irq(STM32_IRQ_TIM3);
	task_enable_irq(STM32_IRQ_TIM2);
}

static void irq_init(void)
{
	/* clear all pending interrupts */
	CPU_NVIC_UNPEND(0) = 0xffffffff;
	/* enable global interrupts */
	asm("cpsie i");
}

static void pmse_init(void)
{
	/* Use 10K-ohm pull down */
	STM32_PMSE_CR |= (1 << 13);
}

void hardware_init(void)
{
	power_init();
	clock_init();
	pins_init();
	timers_init();
	adc_init();
	irq_init();
	pmse_init();
}
