/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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
#include "hwtimer.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

struct mutex adc_lock;

static int watchdog_ain_id;
static int watchdog_delay_ms;

static const struct dma_option dma_adc_option = {
	STM32_DMAC_ADC, (void *)&STM32_ADC_DR,
	STM32_DMA_CCR_MSIZE_32_BIT | STM32_DMA_CCR_PSIZE_32_BIT,
};

static void adc_configure(int ain_id)
{
	/* Select channel to convert */
	STM32_ADC_CHSELR = 1 << ain_id;

	/* Disable DMA */
	STM32_ADC_CFGR1 &= ~0x1;
}

static void adc_continuous_read(int ain_id)
{
	adc_configure(ain_id);

	/* CONT=1 -> continuous mode on */
	STM32_ADC_CFGR1 |= 1 << 13;

	/* Start continuous conversion */
	STM32_ADC_CR |= 1 << 2; /* ADSTART */
}

static void adc_continuous_stop(void)
{
	/* Stop on-going conversion */
	STM32_ADC_CR |= 1 << 4; /* ADSTP */

	/* Wait for conversion to stop */
	while (STM32_ADC_CR & (1 << 4))
		;

	/* CONT=0 -> continuous mode off */
	STM32_ADC_CFGR1 &= ~(1 << 13);
}

static void adc_interval_read(int ain_id, int interval_ms)
{
	adc_configure(ain_id);

	/* EXTEN=01 -> hardware trigger detection on rising edge */
	STM32_ADC_CFGR1 = (STM32_ADC_CFGR1 & ~0xc00) | (1 << 10);

	/* EXTSEL=TRG3 -> Trigger on TIM3_TRGO */
	STM32_ADC_CFGR1 = (STM32_ADC_CFGR1 & ~0x1c0) | (3 << 6);

	__hw_timer_enable_clock(TIM_ADC, 1);

	/* Upcounter, counter disabled, update event only on underflow */
	STM32_TIM_CR1(TIM_ADC) = 0x0004;

	/* TRGO on update event */
	STM32_TIM_CR2(TIM_ADC) = 0x0020;
	STM32_TIM_SMCR(TIM_ADC) = 0x0000;

	/* Auto-reload value */
	STM32_TIM_ARR(TIM_ADC) = interval_ms & 0xffff;

	/* Set prescaler to tick per millisecond */
	STM32_TIM_PSC(TIM_ADC) = (clock_get_freq() / MSEC) - 1;

	/* Start counting */
	STM32_TIM_CR1(TIM_ADC) |= 1;

	/* Start ADC conversion */
	STM32_ADC_CR |= 1 << 2; /* ADSTART */
}

static void adc_interval_stop(void)
{
	/* EXTEN=00 -> hardware trigger detection disabled */
	STM32_ADC_CFGR1 &= ~0xc00;

	/* Set ADSTP to clear ADSTART */
	STM32_ADC_CR |= 1 << 4; /* ADSTP */

	/* Wait for conversion to stop */
	while (STM32_ADC_CR & (1 << 4))
		;

	/* Stop the timer */
	STM32_TIM_CR1(TIM_ADC) &= ~0x1;
}

static int adc_watchdog_enabled(void)
{
	return STM32_ADC_CFGR1 & (1 << 23);
}

static int adc_enable_watchdog_no_lock(void)
{
	/* Select channel */
	STM32_ADC_CFGR1 = (STM32_ADC_CFGR1 & ~0x7c000000) |
			  (watchdog_ain_id << 26);
	adc_configure(watchdog_ain_id);

	/* Clear AWD interupt flag */
	STM32_ADC_ISR = 0x80;
	/* Set Watchdog enable bit on a single channel */
	STM32_ADC_CFGR1 |= (1 << 23) | (1 << 22);
	/* Enable interrupt */
	STM32_ADC_IER |= 1 << 7;

	if (watchdog_delay_ms)
		adc_interval_read(watchdog_ain_id, watchdog_delay_ms);
	else
		adc_continuous_read(watchdog_ain_id);

	return EC_SUCCESS;
}

int adc_enable_watchdog(int ain_id, int high, int low)
{
	int ret;

	mutex_lock(&adc_lock);

	watchdog_ain_id = ain_id;

	/* Set thresholds */
	STM32_ADC_TR = ((high & 0xfff) << 16) | (low & 0xfff);

	ret = adc_enable_watchdog_no_lock();
	mutex_unlock(&adc_lock);
	return ret;
}

static int adc_disable_watchdog_no_lock(void)
{
	if (watchdog_delay_ms)
		adc_interval_stop();
	else
		adc_continuous_stop();

	/* Clear Watchdog enable bit */
	STM32_ADC_CFGR1 &= ~(1 << 23);

	return EC_SUCCESS;
}

int adc_disable_watchdog(void)
{
	int ret;

	mutex_lock(&adc_lock);
	ret = adc_disable_watchdog_no_lock();
	mutex_unlock(&adc_lock);

	return ret;
}

int adc_set_watchdog_delay(int delay_ms)
{
	int resume_watchdog = 0;

	mutex_lock(&adc_lock);
	if (adc_watchdog_enabled()) {
		resume_watchdog = 1;
		adc_disable_watchdog_no_lock();
	}

	watchdog_delay_ms = delay_ms;

	if (resume_watchdog)
		adc_enable_watchdog_no_lock();
	mutex_unlock(&adc_lock);

	return EC_SUCCESS;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;
	int restore_watchdog = 0;

	mutex_lock(&adc_lock);
	if (adc_watchdog_enabled()) {
		restore_watchdog = 1;
		adc_disable_watchdog_no_lock();
	}

	adc_configure(adc->channel);

	/* Clear flags */
	STM32_ADC_ISR = 0xe;

	/* Start conversion */
	STM32_ADC_CR |= 1 << 2; /* ADSTART */

	/* Wait for end of conversion */
	while (!(STM32_ADC_ISR & (1 << 2)))
		;
	/* read converted value */
	value = STM32_ADC_DR;

	if (restore_watchdog)
		adc_enable_watchdog_no_lock();
	mutex_unlock(&adc_lock);

	return value * adc->factor_mul / adc->factor_div + adc->shift;
}

int adc_read_all_channels(int *data)
{
	int i;
	uint32_t channels = 0;
	uint32_t raw_data[ADC_CH_COUNT];
	const struct adc_t *adc;
	int restore_watchdog = 0;
	int ret = EC_SUCCESS;

	mutex_lock(&adc_lock);

	if (adc_watchdog_enabled()) {
		restore_watchdog = 1;
		adc_disable_watchdog_no_lock();
	}

	/* Select all used channels */
	for (i = 0; i < ADC_CH_COUNT; ++i)
		channels |= 1 << adc_channels[i].channel;
	STM32_ADC_CHSELR = channels;

	/* Enable DMA */
	STM32_ADC_CFGR1 |= 0x1;

	dma_start_rx(&dma_adc_option, ADC_CH_COUNT, raw_data);

	/* Clear flags */
	STM32_ADC_ISR = 0xe;

	STM32_ADC_CR |= 1 << 2; /* ADSTART */

	if (dma_wait(STM32_DMAC_ADC)) {
		ret = EC_ERROR_UNKNOWN;
		goto fail; /* goto fail; goto fail; */
	}

	for (i = 0; i < ADC_CH_COUNT; ++i) {
		adc = adc_channels + i;
		data[i] = (raw_data[i] & 0xffff) *
			   adc->factor_mul / adc->factor_div + adc->shift;
	}

fail:
	if (restore_watchdog)
		adc_enable_watchdog_no_lock();
	mutex_unlock(&adc_lock);
	return ret;
}

static void adc_init(void)
{
	/*
	 * If clock is already enabled, and ADC module is enabled
	 * then this is a warm reboot and ADC is already initialized.
	 */
	if (STM32_RCC_APB2ENR & (1 << 9) && (STM32_ADC_CR & STM32_ADC_CR_ADEN))
		return;

	/* Enable ADC clock */
	STM32_RCC_APB2ENR |= (1 << 9);
	/* check HSI14 in RCC ? ON by default */

	/* ADC calibration (done with ADEN = 0) */
	STM32_ADC_CR = STM32_ADC_CR_ADCAL; /* set ADCAL = 1, ADC off */
	/* wait for the end of calibration */
	while (STM32_ADC_CR & STM32_ADC_CR_ADCAL)
		;

	/* As per ST recommendation, ensure two cycles before setting ADEN */
	asm volatile("nop; nop;");

	/* ADC enabled */
	STM32_ADC_CR = STM32_ADC_CR_ADEN;
	while (!(STM32_ADC_ISR & STM32_ADC_ISR_ADRDY))
		;

	/* Single conversion, right aligned, 12-bit */
	STM32_ADC_CFGR1 = 1 << 12; /* (1 << 15) => AUTOOFF */;
	/* clock is ADCCLK */
	STM32_ADC_CFGR2 = 0;
	/* Sampling time : 13.5 ADC clock cycles. */
	STM32_ADC_SMPR = 2;
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);
