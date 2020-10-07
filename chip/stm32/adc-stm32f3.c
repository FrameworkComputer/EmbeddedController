/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define ADC_SINGLE_READ_TIMEOUT 3000 /* 3 ms */

#define SMPR1_EXPAND(v) ((v) | ((v) << 3) | ((v) << 6) | ((v) << 9) | \
			 ((v) << 12) | ((v) << 15) | ((v) << 18) | \
			 ((v) << 21))
#define SMPR2_EXPAND(v) (SMPR1_EXPAND(v) | ((v) << 24) | ((v) << 27))

/* Default ADC sample time = 13.5 cycles */
#ifndef CONFIG_ADC_SAMPLE_TIME
#define CONFIG_ADC_SAMPLE_TIME 2
#endif

struct mutex adc_lock;

static int watchdog_ain_id;

static inline void adc_set_channel(int sample_id, int channel)
{
	uint32_t mask, val;
	volatile uint32_t *sqr_reg;

	if (sample_id < 6) {
		mask = 0x1f << (sample_id * 5);
		val = channel << (sample_id * 5);
		sqr_reg = &STM32_ADC_SQR3;
	} else if (sample_id < 12) {
		mask = 0x1f << ((sample_id - 6) * 5);
		val = channel << ((sample_id - 6) * 5);
		sqr_reg = &STM32_ADC_SQR2;
	} else {
		mask = 0x1f << ((sample_id - 12) * 5);
		val = channel << ((sample_id - 12) * 5);
		sqr_reg = &STM32_ADC_SQR1;
	}

	*sqr_reg = (*sqr_reg & ~mask) | val;
}

static void adc_configure(int ain_id)
{
	/* Set ADC channel */
	adc_set_channel(0, ain_id);

	/* Disable DMA */
	STM32_ADC_CR2 &= ~BIT(8);

	/* Disable scan mode */
	STM32_ADC_CR1 &= ~BIT(8);
}

static void __attribute__((unused)) adc_configure_all(void)
{
	int i;

	/* Set ADC channels */
	STM32_ADC_SQR1 = (ADC_CH_COUNT - 1) << 20;
	for (i = 0; i < ADC_CH_COUNT; ++i)
		adc_set_channel(i, adc_channels[i].channel);

	/* Enable DMA */
	STM32_ADC_CR2 |= BIT(8);

	/* Enable scan mode */
	STM32_ADC_CR1 |= BIT(8);
}

static inline int adc_powered(void)
{
	return STM32_ADC_CR2 & BIT(0);
}

static inline int adc_conversion_ended(void)
{
	return STM32_ADC_SR & BIT(1);
}

static int adc_watchdog_enabled(void)
{
	return STM32_ADC_CR1 & BIT(23);
}

static int adc_enable_watchdog_no_lock(void)
{
	/* Fail if watchdog already enabled */
	if (adc_watchdog_enabled())
		return EC_ERROR_UNKNOWN;

	/* Set channel */
	STM32_ADC_SQR3 = watchdog_ain_id;
	STM32_ADC_SQR1 = 0;
	STM32_ADC_CR1 = (STM32_ADC_CR1 & ~0x1f) | watchdog_ain_id;

	/* Clear interrupt bit */
	STM32_ADC_SR &= ~0x1;

	/* AWDSGL=1, SCAN=1, AWDIE=1, AWDEN=1 */
	STM32_ADC_CR1 |= BIT(9) | BIT(8) | BIT(6) | BIT(23);

	/* Disable DMA */
	STM32_ADC_CR2 &= ~BIT(8);

	/* CONT=1 */
	STM32_ADC_CR2 |= BIT(1);

	/* Start conversion */
	STM32_ADC_CR2 |= BIT(0);

	return EC_SUCCESS;
}

int adc_enable_watchdog(int ain_id, int high, int low)
{
	int ret;

	if (!adc_powered())
		return EC_ERROR_UNKNOWN;

	mutex_lock(&adc_lock);

	watchdog_ain_id = ain_id;

	/* Set thresholds */
	STM32_ADC_HTR = high & 0xfff;
	STM32_ADC_LTR = low & 0xfff;

	ret = adc_enable_watchdog_no_lock();
	mutex_unlock(&adc_lock);
	return ret;
}

static int adc_disable_watchdog_no_lock(void)
{
	/* Fail if watchdog not running */
	if (!adc_watchdog_enabled())
		return EC_ERROR_UNKNOWN;

	/* AWDEN=0, AWDIE=0 */
	STM32_ADC_CR1 &= ~BIT(23) & ~BIT(6);

	/* CONT=0 */
	STM32_ADC_CR2 &= ~BIT(1);

	return EC_SUCCESS;
}

int adc_disable_watchdog(void)
{
	int ret;

	if (!adc_powered())
		return EC_ERROR_UNKNOWN;

	mutex_lock(&adc_lock);
	ret = adc_disable_watchdog_no_lock();
	mutex_unlock(&adc_lock);
	return ret;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;
	int restore_watchdog = 0;
	timestamp_t deadline;

	if (!adc_powered())
		return EC_ERROR_UNKNOWN;

	mutex_lock(&adc_lock);

	if (adc_watchdog_enabled()) {
		restore_watchdog = 1;
		adc_disable_watchdog_no_lock();
	}

	adc_configure(adc->channel);

	/* Clear EOC bit */
	STM32_ADC_SR &= ~BIT(1);

	/* Start conversion (Note: For now only confirmed on F4) */
#if defined(CHIP_FAMILY_STM32F4)
	STM32_ADC_CR2 |= STM32_ADC_CR2_ADON | STM32_ADC_CR2_SWSTART;
#else
	STM32_ADC_CR2 |= STM32_ADC_CR2_ADON;
#endif

	/* Wait for EOC bit set */
	deadline.val = get_time().val + ADC_SINGLE_READ_TIMEOUT;
	value = ADC_READ_ERROR;
	do {
		if (adc_conversion_ended()) {
			value = STM32_ADC_DR & ADC_READ_MAX;
			break;
		}
	} while (!timestamp_expired(deadline, NULL));

	if (restore_watchdog)
		adc_enable_watchdog_no_lock();

	mutex_unlock(&adc_lock);
	return (value == ADC_READ_ERROR) ? ADC_READ_ERROR :
	       value * adc->factor_mul / adc->factor_div + adc->shift;
}

static void adc_init(void)
{
	/*
	 * Enable ADC clock.
	 * APB2 clock is 16MHz. ADC clock prescaler is /2.
	 * So the ADC clock is 8MHz.
	 */
	clock_enable_module(MODULE_ADC, 1);

	/*
	 * ADC clock is divided with respect to AHB, so no delay needed
	 * here. If ADC clock is the same as AHB, a read on ADC
	 * register is needed here.
	 */

	if (!adc_powered()) {
		/* Power on ADC module */
		STM32_ADC_CR2 |= STM32_ADC_CR2_ADON;

		/* Reset calibration */
		STM32_ADC_CR2 |= STM32_ADC_CR2_RSTCAL;
		while (STM32_ADC_CR2 & STM32_ADC_CR2_RSTCAL)
			;

		/* A/D Calibrate */
		STM32_ADC_CR2 |= STM32_ADC_CR2_CAL;
		while (STM32_ADC_CR2 & STM32_ADC_CR2_CAL)
			;
	}

	/* Set right alignment */
	STM32_ADC_CR2 &= ~STM32_ADC_CR2_ALIGN;

	/* Set sample time of all channels */
	STM32_ADC_SMPR1 = SMPR1_EXPAND(CONFIG_ADC_SAMPLE_TIME);
	STM32_ADC_SMPR2 = SMPR2_EXPAND(CONFIG_ADC_SAMPLE_TIME);
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);
