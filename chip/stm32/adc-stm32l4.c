/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC drivers for STM32L4xx as well as STM32L5xx. */

#include "adc.h"
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

struct adc_profile_t {
	/* Register values. */
	uint32_t cfgr1_reg;
	uint32_t cfgr2_reg;
	uint32_t smpr_reg; /* Default Sampling Rate */
	uint32_t ier_reg;
	/* DMA config. */
	const struct dma_option *dma_option;
	/* Size of DMA buffer, in units of ADC_CH_COUNT. */
	int dma_buffer_size;
};

#ifdef CONFIG_ADC_PROFILE_SINGLE
#ifndef CONFIG_ADC_SAMPLE_TIME
#define CONFIG_ADC_SAMPLE_TIME STM32_ADC_SMPR_12_5_CY
#endif
#endif

#define ADC_CALIBRATION_TIMEOUT_US 100000U
#define ADC_ENABLE_TIMEOUT_US 200000U
#define ADC_CONVERSION_TIMEOUT_US 200000U

static uint8_t adc1_initialized;

#ifdef CONFIG_ADC_PROFILE_FAST_CONTINUOUS
#error "Continuous ADC sampling not implemented for STM32L4/5"

#ifndef CONFIG_ADC_SAMPLE_TIME
#define CONFIG_ADC_SAMPLE_TIME STM32_ADC_SMPR_1_5_CY
#endif

static const struct dma_option dma_continuous = {
	STM32_DMAC_ADC,
	(void *)&STM32_ADC_DR,
	STM32_DMA_CCR_MSIZE_32_BIT | STM32_DMA_CCR_PSIZE_32_BIT |
		STM32_DMA_CCR_CIRC,
};

static const struct adc_profile_t profile = {
	/* Sample all channels continuously using DMA */
	.cfgr1_reg = STM32_ADC_CFGR1_OVRMOD | STM32_ADC_CFGR1_CONT |
		     STM32_ADC_CFGR1_DMACFG,
	.cfgr2_reg = 0,
	.smpr_reg = CONFIG_ADC_SAMPLE_TIME,
	/* Fire interrupt at end of sequence. */
	.ier_reg = STM32_ADC_IER_EOSEQIE,
	.dma_option = &dma_continuous,
	/* Double-buffer our samples. */
	.dma_buffer_size = 2,
};
#endif

static void adc_init(void)
{
	/*
	 * If clock is already enabled, and ADC module is enabled
	 * then this is a warm reboot and ADC is already initialized.
	 */

	if (STM32_RCC_AHB2ENR & STM32_RCC_AHB2ENR_ADCEN &&
	    (STM32_ADC1_CR & STM32_ADC1_CR_ADEN))
		return;

	/* Enable ADC clock */
	clock_enable_module(MODULE_ADC, 1);

	/* set ADC clock to 20MHz */
	STM32_ADC1_CCR &= ~0x003C0000;
	STM32_ADC1_CCR |= 0x00080000;

	STM32_RCC_AHB2ENR |= STM32_RCC_HB2_GPIOA;
	STM32_RCC_AHB2ENR |= STM32_RCC_HB2_GPIOB;

	/* Set ADC data resolution */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_CONT;
	/* Set ADC conversion data alignment */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_ALIGN;
	/* Set ADC delayed conversion mode */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_AUTDLY;
}

BUILD_ASSERT(CONFIG_ADC_SAMPLE_TIME > 0 && CONFIG_ADC_SAMPLE_TIME <= 8);

static void adc_configure_channel(int ain_id, enum stm32_adc_smpr sample_time)
{
	/* Select Sampling time for channel to convert */
	if (sample_time == STM32_ADC_SMPR_DEFAULT)
		sample_time = CONFIG_ADC_SAMPLE_TIME;

	if (ain_id <= 10) {
		STM32_ADC1_SMPR1 &= ~(7 << ((ain_id - 1) * 3));
		STM32_ADC1_SMPR1 |= ((sample_time - 1) << ((ain_id - 1) * 3));
	} else {
		STM32_ADC1_SMPR2 &= ~(7 << ((ain_id - 11) * 3));
		STM32_ADC1_SMPR2 |= ((sample_time - 1) << ((ain_id - 11) * 3));
	}
}

static void adc_select_channel(int ain_id)
{
	/* Setup an "injected sequence" consisting of only this one channel. */
	STM32_ADC1_JSQR = ain_id << 8;
}

static void stm32_adc1_isr_clear(uint32_t bitmask)
{
	/* Write 1 to clear */
	STM32_ADC1_ISR = bitmask;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;

	int value = 0;
	uint32_t wait_loop_index;

	mutex_lock(&adc_lock);

	if (adc1_initialized == 0) {
		adc_init();

		/* Configure Channel N */
		for (uint8_t i = 0; i < ADC_CH_COUNT; i++) {
			const struct adc_t *adc = adc_channels + i;

			adc_configure_channel(adc->channel, adc->sample_time);
		}

		/* Disable DMA */
		STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_DMAEN;

		if ((STM32_ADC1_CR & STM32_ADC1_CR_ADEN) !=
		    STM32_ADC1_CR_ADEN) {
			/* Disable ADC deep power down (enabled by default after
			 * reset state)
			 */
			STM32_ADC1_CR &= ~STM32_ADC1_CR_DEEPPWD;
			/* Enable ADC internal voltage regulator */
			STM32_ADC1_CR |= STM32_ADC1_CR_ADVREGEN;
		}

		/* Delay for ADC internal voltage regulator stabilization. */
		udelay(20);

		/* Run ADC self calibration */
		STM32_ADC1_CR |= STM32_ADC1_CR_ADCAL;

		/* wait for the end of calibration */
		wait_loop_index = ((ADC_CALIBRATION_TIMEOUT_US *
				    (CPU_CLOCK / (100000 * 2))) /
				   10);
		while (STM32_ADC1_CR & STM32_ADC1_CR_ADCAL) {
			if (wait_loop_index-- == 0)
				break;
		}

		/* Enable ADC */
		stm32_adc1_isr_clear(STM32_ADC1_ISR_ADRDY);
		STM32_ADC1_CR |= STM32_ADC1_CR_ADEN;
		wait_loop_index =
			((ADC_ENABLE_TIMEOUT_US * (CPU_CLOCK / (100000 * 2))) /
			 10);
		while (!(STM32_ADC1_ISR & STM32_ADC1_ISR_ADRDY)) {
			wait_loop_index--;
			if (wait_loop_index == 0)
				break;
		}
		stm32_adc1_isr_clear(STM32_ADC1_ISR_ADRDY);

		adc1_initialized = 1;
	}

	/* Configure Injected Channel N */
	adc_select_channel(adc->channel);

	/* Start injected conversion */
	STM32_ADC1_CR |= BIT(3); /* JADSTART */

	/* Wait for end of injected conversion */
	wait_loop_index =
		((ADC_CONVERSION_TIMEOUT_US * (CPU_CLOCK / (100000 * 2))) / 10);
	while (!(STM32_ADC1_ISR & BIT(6))) {
		if (wait_loop_index-- == 0)
			break;
	}

	/* Clear JEOS bit */
	stm32_adc1_isr_clear(BIT(6));

	/* read converted value */
	value = STM32_ADC1_JDR1;

	mutex_unlock(&adc_lock);

	return value * adc->factor_mul / adc->factor_div + adc->shift;
}

void adc_disable(void)
{
	/* Disable ADC */
	/* Do not Set ADDIS when ADC is disabled */
	adc1_initialized = 0;

	if (STM32_ADC1_CR & STM32_ADC1_CR_ADEN)
		STM32_ADC1_CR |= STM32_ADC1_CR_ADDIS;

	/*
	 * Note that the ADC is not in OFF state immediately.
	 * Once the ADC is effectively put into OFF state,
	 * STM32_ADC_CR_ADDIS bit will be cleared by hardware.
	 */
}
