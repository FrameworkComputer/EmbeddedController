/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
	uint32_t smpr_reg;	/* Default Sampling Rate */
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

#if defined(CHIP_FAMILY_STM32L4)
#define ADC_CALIBRATION_TIMEOUT_US	100000U
#define ADC_ENABLE_TIMEOUT_US		200000U
#define ADC_CONVERSION_TIMEOUT_US	200000U

#define NUMBER_OF_ADC_CHANNEL   2
uint8_t adc1_initialized;
#endif

#ifdef CONFIG_ADC_PROFILE_FAST_CONTINUOUS

#ifndef CONFIG_ADC_SAMPLE_TIME
#define CONFIG_ADC_SAMPLE_TIME STM32_ADC_SMPR_1_5_CY
#endif

static const struct dma_option dma_continuous = {
	STM32_DMAC_ADC, (void *)&STM32_ADC_DR,
	STM32_DMA_CCR_MSIZE_32_BIT | STM32_DMA_CCR_PSIZE_32_BIT |
	STM32_DMA_CCR_CIRC,
};

static const struct adc_profile_t profile = {
	/* Sample all channels continuously using DMA */
	.cfgr1_reg = STM32_ADC_CFGR1_OVRMOD |
		     STM32_ADC_CFGR1_CONT |
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

static void adc_init(const struct adc_t *adc)
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
	STM32_ADC1_CCR |=  0x00080000;

	STM32_RCC_AHB2ENR |= STM32_RCC_HB2_GPIOA;
	STM32_RCC_AHB2ENR |= STM32_RCC_HB2_GPIOB;

	/* Set ADC data resolution */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_CONT;
	/* Set ADC conversion data alignment */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_ALIGN;
	/* Set ADC delayed conversion mode */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_AUTDLY;
}

static void adc_configure(int ain_id, int ain_rank,
		enum stm32_adc_smpr sample_rate)
{
	/* Select Sampling time and channel to convert */
	if (ain_id <= 10)	{
		STM32_ADC1_SMPR1 &= ~(7 << ((ain_id - 1) * 3));
		STM32_ADC1_SMPR1 |= (sample_rate << ((ain_id - 1) * 3));
	} else	{
		STM32_ADC1_SMPR2 &= ~(7 << ((ain_id - 11) * 3));
		STM32_ADC1_SMPR2 |= (sample_rate << ((ain_id - 11) * 3));
	}

	/* Setup Rank */
	STM32_ADC1_JSQR &= ~(0x03);
	STM32_ADC1_JSQR |= NUMBER_OF_ADC_CHANNEL - 1;

	STM32_ADC1_JSQR &= ~(0x1F << (((ain_rank - 1) * 6) + 8));
	STM32_ADC1_JSQR |= (ain_id << (((ain_rank - 1) * 6) + 8));

	/* Disable DMA */
	STM32_ADC1_CFGR &= ~STM32_ADC1_CFGR_DMAEN;
}

int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;

	int value = 0;
	uint32_t wait_loop_index;

	mutex_lock(&adc_lock);

	if (adc1_initialized == 0) {
		adc_init(adc);

		/* Configure Injected Channel N */
		for (uint8_t i = 0; i < NUMBER_OF_ADC_CHANNEL; i++) {
			const struct adc_t *adc = adc_channels + i;

			adc_configure(adc->channel, adc->rank,
				      adc->sample_rate);
		}

		if ((STM32_ADC1_CR & STM32_ADC1_CR_ADEN) !=
		    STM32_ADC1_CR_ADEN) {
			/* Disable ADC deep power down (enabled by default after
			 * reset state)
			 */
			STM32_ADC1_CR &= ~STM32_ADC1_CR_DEEPPWD;
			/* Enable ADC internal voltage regulator */
			STM32_ADC1_CR |= STM32_ADC1_CR_ADVREGEN;
		}

		/*
		 * Delay for ADC internal voltage regulator stabilization.
		 * Compute number of CPU cycles to wait for, from delay in us.
		 *
		 * Note: Variable divided by 2 to compensate partially
		 * CPU processing cycles (depends on compilation optimization).
		 *
		 * Note: If system core clock frequency is below 200kHz, wait
		 * time is only a few CPU processing cycles.
		 */
		wait_loop_index = ((20 * (80000000 / (100000 * 2))) / 10);
		while (wait_loop_index-- != 0)
			;

		/* Run ADC self calibration */
		STM32_ADC1_CR |= STM32_ADC1_CR_ADCAL;

		/* wait for the end of calibration */
		wait_loop_index = ((ADC_CALIBRATION_TIMEOUT_US *
				(CPU_CLOCK / (100000 * 2))) / 10);
		while (STM32_ADC1_CR & STM32_ADC1_CR_ADCAL) {
			if (wait_loop_index-- == 0)
				break;
		}

		/* Enable ADC */
		STM32_ADC1_ISR |= STM32_ADC1_ISR_ADRDY;
		STM32_ADC1_CR |= STM32_ADC1_CR_ADEN;
		wait_loop_index = ((ADC_ENABLE_TIMEOUT_US *
				(CPU_CLOCK / (100000 * 2))) / 10);
		while (!(STM32_ADC1_ISR & STM32_ADC1_ISR_ADRDY)) {
			wait_loop_index--;
			if (wait_loop_index == 0)
				break;
		}

		adc1_initialized = 1;
	}

	/* Start injected conversion */
	STM32_ADC1_CR |= BIT(3); /* JADSTART */

	/* Wait for end of injected conversion */
	wait_loop_index = ((ADC_CONVERSION_TIMEOUT_US *
			(CPU_CLOCK / (100000 * 2))) / 10);
	while (!(STM32_ADC1_ISR & BIT(6))) {
		if (wait_loop_index-- == 0)
			break;
	}

	/* Clear JEOS bit */
	STM32_ADC1_ISR |= BIT(6);

	/* read converted value */
	if (adc->rank == 1)
		value = STM32_ADC1_JDR1;
	if (adc->rank == 2)
		value = STM32_ADC1_JDR2;

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
