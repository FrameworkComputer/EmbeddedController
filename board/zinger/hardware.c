/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Hardware initialization and common functions */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "cpu.h"
#include "registers.h"
#include "sha1.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

static void system_init(void)
{
	/* Enable access to RCC CSR register and RTC backup registers */
	STM32_PWR_CR |= 1 << 8;

	/* switch on LSI */
	STM32_RCC_CSR |= 1 << 0;
	/* Wait for LSI to be ready */
	while (!(STM32_RCC_CSR & (1 << 1)))
		;
	/* re-configure RTC if needed */
	if ((STM32_RCC_BDCR & 0x00018300) != 0x00008200) {
		/* the RTC settings are bad, we need to reset it */
		STM32_RCC_BDCR |= 0x00010000;
		/* Enable RTC and use LSI as clock source */
		STM32_RCC_BDCR = (STM32_RCC_BDCR & ~0x00018300) | 0x00008200;
	}
}

static void power_init(void)
{
	/* enable SYSCFG, COMP, ADC, SPI1, USART1 */
	STM32_RCC_APB2ENR = 0x00005201;
	/* enable TIM2, TIM3, TIM14, PWR */
	STM32_RCC_APB1ENR = 0x10000103;
	/* enable DMA, SRAM, CRC, GPA, GPB, GPF */
	STM32_RCC_AHBENR  = 0x460045;
}

/* GPIO setting helpers */
#define OUT(n) (1 << ((n) * 2))
#define AF(n) (2 << ((n) * 2))
#define ANALOG(n) (3 << ((n) * 2))
#define HIGH(n) (1 << (n))
#define ODR(n) (1 << (n))
#define HISPEED(n) (3 << ((n) * 2))
#define AFx(n, x) (x << (((n) % 8) * 4))

static void pins_init(void)
{
	/* Pin usage:
	 * PA0  (OUT - GPIO)       : Wakeup on Vnc / Threshold
	 * PA1  (ANALOG - ADC_IN1) : CC sense
	 * PA2  (ANALOG - ADC_IN2) : Current sense
	 * PA3  (ANALOG - ADC_IN3) : Voltage sense
	 * PA4  (OUT - OD GPIO)    : PD TX enable
	 * PA5  (AF0 - SPI1_SCK)   : TX clock in
	 * PA6  (AF0 - SPI1_MISO)  : PD TX
	 * PA7  (AF5 - TIM3_CH2)   : PD RX
	 * PA9  (AF1 - UART1_TX)   : [DEBUG] UART TX
	 * PA10 (AF1 - UART1_RX)   : [DEBUG] UART RX
	 * PA13 (OUT - GPIO)       : voltage select[0]
	 * PA14 (OUT - GPIO)       : voltage select[1]
	 * PB1  (AF0 - TIM14_CH1)  : TX clock out
	 * PF0  (OUT - GPIO)       : LM5050 FET driver off
	 * PF1  (OUT - GPIO)       : discharge FET
	 */
	STM32_GPIO_ODR(GPIO_A) = HIGH(0) | HIGH(4);
	STM32_GPIO_AFRL(GPIO_A) = AFx(7, 1);
	STM32_GPIO_AFRH(GPIO_A) = AFx(9, 1) | AFx(10, 1);
	STM32_GPIO_OTYPER(GPIO_A) = ODR(4);
	STM32_GPIO_OSPEEDR(GPIO_A) = HISPEED(5) | HISPEED(6) | HISPEED(7);
	STM32_GPIO_MODER(GPIO_A) = OUT(0) | ANALOG(1) | ANALOG(2) | ANALOG(3)
				 | OUT(4) | AF(5) /*| AF(6)*/ | AF(7) | AF(9)
				 | AF(10) | OUT(13) | OUT(14);
	/* set PF0 / PF1 as output */
	STM32_GPIO_ODR(GPIO_F) = 0;
	STM32_GPIO_MODER(GPIO_F) = OUT(0) | OUT(1);
	STM32_GPIO_OTYPER(GPIO_F) = 0;

	/* Set PB1 as AF0 (TIM14_CH1) */
	STM32_GPIO_OSPEEDR(GPIO_B) = HISPEED(1);
	STM32_GPIO_MODER(GPIO_B) = AF(1);
}

static void adc_init(void)
{
	/* Only do the calibration if the ADC is off  */
	if (!(STM32_ADC_CR & 1)) {
		/* ADC calibration */
		STM32_ADC_CR = STM32_ADC_CR_ADCAL; /* set ADCAL = 1, ADC off */
		/* wait for the end of calibration */
		while (STM32_ADC_CR & STM32_ADC_CR_ADCAL)
			;
	}
	/* As per ST recommendation, ensure two cycles before setting ADEN */
	asm volatile("nop; nop;");
	/* ADC enabled */
	STM32_ADC_CR = STM32_ADC_ISR_ADRDY;
	while (!(STM32_ADC_ISR & STM32_ADC_ISR_ADRDY))
		;
	/* Single conversion, right aligned, 12-bit */
	STM32_ADC_CFGR1 = 1 << 12; /* (1 << 15) => AUTOOFF */;
	/* clock is ADCCLK */
	STM32_ADC_CFGR2 = 0;
	/* Sampling time : 71.5 ADC clock cycles, about 5us */
	STM32_ADC_SMPR = 6;
	/* Disable interrupts */
	STM32_ADC_IER = 0;
	/* Analog watchdog IRQ */
	task_enable_irq(STM32_IRQ_ADC_COMP);
}

static void uart_init(void)
{
	/* set baudrate */
	STM32_USART_BRR(UARTN_BASE) =
		DIV_ROUND_NEAREST(CPU_CLOCK, CONFIG_UART_BAUD_RATE);
	/* UART enabled, 8 Data bits, oversampling x16, no parity */
	STM32_USART_CR1(UARTN_BASE) =
		STM32_USART_CR1_UE | STM32_USART_CR1_TE | STM32_USART_CR1_RE;
	/* 1 stop bit, no fancy stuff */
	STM32_USART_CR2(UARTN_BASE) = 0x0000;
	/* DMA disabled, special modes disabled, error interrupt disabled */
	STM32_USART_CR3(UARTN_BASE) = 0x0000;
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
	STM32_TIM_DIER(2) = 0;
	task_enable_irq(STM32_IRQ_TIM2);
}

static void irq_init(void)
{
	/* clear all pending interrupts */
	CPU_NVIC_UNPEND(0) = 0xffffffff;
	/* enable global interrupts */
	asm("cpsie i");
}

extern void runtime_init(void);
void hardware_init(void)
{
	power_init();
	system_init();
	runtime_init(); /* sets clock */
	pins_init();
	uart_init();
	timers_init();
	watchdog_init();
	adc_init();
	irq_init();
}

static int watchdog_ain_id, watchdog_ain_high, watchdog_ain_low;

static int adc_enable_last_watchdog(void)
{
	return adc_enable_watchdog(watchdog_ain_id, watchdog_ain_high,
			    watchdog_ain_low);
}

static inline int adc_watchdog_enabled(void)
{
	return STM32_ADC_CFGR1 & (1 << 23);
}

int adc_read_channel(enum adc_channel ch)
{
	int value;
	int watchdog_enabled = adc_watchdog_enabled();

	if (watchdog_enabled)
		adc_disable_watchdog();

	/* Select channel to convert */
	STM32_ADC_CHSELR = 1 << ch;
	/* Clear flags */
	STM32_ADC_ISR = 0x8e;
	/* Start conversion */
	STM32_ADC_CR |= 1 << 2; /* ADSTART */
	/* Wait for end of conversion */
	while (!(STM32_ADC_ISR & (1 << 2)))
		;
	/* read converted value */
	value = STM32_ADC_DR;

	if (watchdog_enabled)
		adc_enable_last_watchdog();

	return value;
}

int adc_enable_watchdog(int ch, int high, int low)
{
	/* store last watchdog setup */
	watchdog_ain_id = ch;
	watchdog_ain_high = high;
	watchdog_ain_low = low;

	/* Set thresholds */
	STM32_ADC_TR = ((high & 0xfff) << 16) | (low & 0xfff);
	/* Select channel to convert */
	STM32_ADC_CHSELR = 1 << ch;
	/* Clear flags */
	STM32_ADC_ISR = 0x8e;
	/* Set Watchdog enable bit on a single channel / continuous mode */
	STM32_ADC_CFGR1 = (ch << 26) | (1 << 23) | (1 << 22)
			|  (1 << 13) | (1 << 12);
	/* Enable watchdog interrupt */
	STM32_ADC_IER = 1 << 7;
	/* Start continuous conversion */
	STM32_ADC_CR |= 1 << 2; /* ADSTART */

	return EC_SUCCESS;
}

int adc_disable_watchdog(void)
{
	/* Stop on-going conversion */
	STM32_ADC_CR |= 1 << 4; /* ADSTP */
	/* Wait for conversion to stop */
	while (STM32_ADC_CR & (1 << 4))
		;
	/* CONT=0 -> continuous mode off / Clear Watchdog enable */
	STM32_ADC_CFGR1 = 1 << 12;
	/* Disable interrupt */
	STM32_ADC_IER = 0;
	/* Clear flags */
	STM32_ADC_ISR = 0x8e;

	return EC_SUCCESS;
}

/* ---- flash handling ---- */

/*
 * Approximate number of CPU cycles per iteration of the loop when polling
 * the flash status
 */
#define CYCLE_PER_FLASH_LOOP 10

/* Flash page programming timeout.  This is 2x the datasheet max. */
#define FLASH_TIMEOUT_US 16000
#define FLASH_TIMEOUT_LOOP \
	(FLASH_TIMEOUT_US * (CPU_CLOCK / SECOND) / CYCLE_PER_FLASH_LOOP)

/* Flash unlocking keys */
#define KEY1    0x45670123
#define KEY2    0xCDEF89AB

/* Lock bits for FLASH_CR register */
#define PG       (1<<0)
#define PER      (1<<1)
#define STRT     (1<<6)
#define CR_LOCK  (1<<7)

int flash_write_rw(int offset, int size, const char *data)
{
	uint16_t *address = (uint16_t *)
		(CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF + offset);
	int res = EC_SUCCESS;
	int i;

	if ((uint32_t)address > CONFIG_FLASH_BASE + CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	/* unlock CR if needed */
	if (STM32_FLASH_CR & CR_LOCK) {
		STM32_FLASH_KEYR = KEY1;
		STM32_FLASH_KEYR = KEY2;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;
	/* set the ProGram bit */
	STM32_FLASH_CR |= PG;

	for (; size > 0; size -= sizeof(uint16_t)) {
		/* wait to be ready  */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP);
		     i++)
			;
		/* write the half word */
		*address++ = data[0] + (data[1] << 8);
		data += 2;
		/* Wait for writes to complete */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP);
		     i++)
			;
		if (i == FLASH_TIMEOUT_LOOP) {
			res = EC_ERROR_TIMEOUT;
			goto exit_wr;
		}
		/* Check for error conditions - erase failed, voltage error,
		 * protection error */
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_wr;
		}
	}

exit_wr:
	STM32_FLASH_CR &= ~PG;
	STM32_FLASH_CR = CR_LOCK;

	return res;
}

int flash_erase_rw(void)
{
	int res = EC_SUCCESS;
	int offset = CONFIG_FW_RW_OFF;
	int size = CONFIG_FW_RW_SIZE;

	/* unlock CR if needed */
	if (STM32_FLASH_CR & CR_LOCK) {
		STM32_FLASH_KEYR = KEY1;
		STM32_FLASH_KEYR = KEY2;
	}

	/* Clear previous error status */
	STM32_FLASH_SR = 0x34;
	/* set PER bit */
	STM32_FLASH_CR |= PER;

	for (; size > 0; size -= CONFIG_FLASH_ERASE_SIZE,
	     offset += CONFIG_FLASH_ERASE_SIZE) {
		int i;
		/* select page to erase */
		STM32_FLASH_AR = CONFIG_FLASH_BASE + offset;
		/* set STRT bit : start erase */
		STM32_FLASH_CR |= STRT;


		/* Wait for erase to complete */
		for (i = 0; (STM32_FLASH_SR & 1) && (i < FLASH_TIMEOUT_LOOP);
		     i++)
			;
		if (i == FLASH_TIMEOUT_LOOP) {
			res = EC_ERROR_TIMEOUT;
			goto exit_er;
		}

		/*
		 * Check for error conditions - erase failed, voltage error,
		 * protection error
		 */
		if (STM32_FLASH_SR & 0x14) {
			res = EC_ERROR_UNKNOWN;
			goto exit_er;
		}
	}

exit_er:
	STM32_FLASH_CR &= ~PER;
	STM32_FLASH_CR = CR_LOCK;

	return res;
}

static struct sha1_ctx ctx;
uint8_t *flash_hash_rw(void)
{
	sha1_init(&ctx);
	sha1_update(&ctx, (void *)CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF,
		    CONFIG_FW_RW_SIZE - 32);
	return sha1_final(&ctx);
}
