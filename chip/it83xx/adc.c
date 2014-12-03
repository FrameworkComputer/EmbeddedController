/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT8380 ADC module for Chrome EC */

#include "adc.h"
#include "adc_chip.h"
#include "clock.h"
#include "console.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Data structure of ADC channel control registers. */
const struct adc_ctrl_t adc_ctrl_regs[] = {
	{&IT83XX_ADC_VCH0CTL, &IT83XX_ADC_VCH0DATM, &IT83XX_ADC_VCH0DATL,
		&IT83XX_GPIO_GPCRI0},
	{&IT83XX_ADC_VCH1CTL, &IT83XX_ADC_VCH1DATM, &IT83XX_ADC_VCH1DATL,
		&IT83XX_GPIO_GPCRI1},
	{&IT83XX_ADC_VCH2CTL, &IT83XX_ADC_VCH2DATM, &IT83XX_ADC_VCH2DATL,
		&IT83XX_GPIO_GPCRI2},
	{&IT83XX_ADC_VCH3CTL, &IT83XX_ADC_VCH3DATM, &IT83XX_ADC_VCH3DATL,
		&IT83XX_GPIO_GPCRI3},
	{&IT83XX_ADC_VCH4CTL, &IT83XX_ADC_VCH4DATM, &IT83XX_ADC_VCH4DATL,
		&IT83XX_GPIO_GPCRI4},
	{&IT83XX_ADC_VCH5CTL, &IT83XX_ADC_VCH5DATM, &IT83XX_ADC_VCH5DATL,
		&IT83XX_GPIO_GPCRI5},
	{&IT83XX_ADC_VCH6CTL, &IT83XX_ADC_VCH6DATM, &IT83XX_ADC_VCH6DATL,
		&IT83XX_GPIO_GPCRI6},
	{&IT83XX_ADC_VCH7CTL, &IT83XX_ADC_VCH7DATM, &IT83XX_ADC_VCH7DATL,
		&IT83XX_GPIO_GPCRI7},
};

static void adc_enable_channel(int ch)
{
	if (ch < 4)
		/*
		 * for channel 0, 1, 2, and 3
		 * bit4 ~ bit0 : indicates voltage channel[x]
		 *               input is selected for measurement (enable)
		 * bit 7 : W/C data valid flag
		 */
		*adc_ctrl_regs[ch].adc_ctrl = 0x80 + ch;
	else
		/*
		 * for channel 4, 5, 6, and 7
		 * bit4 : voltage channel enable (ch 4~7 only)
		 * bit7 : W/C data valid flag
		 */
		*adc_ctrl_regs[ch].adc_ctrl = 0x90;

	/* bit 0 : adc module enable */
	IT83XX_ADC_ADCCFG |= 0x01;
}

static void adc_disable_channel(int ch)
{
	if (ch < 4)
		/*
		 * for channel 0, 1, 2, and 3
		 * bit4 ~ bit0 : indicates voltage channel[x]
		 *               input is selected for measurement (disable)
		 * bit 7 : W/C data valid flag
		 */
		*adc_ctrl_regs[ch].adc_ctrl = 0x9F;
	else
		/*
		 * for channel 4, 5, 6, and 7
		 * bit4 : voltage channel disable (ch 4~7 only)
		 * bit7 : W/C data valid flag
		 */
		*adc_ctrl_regs[ch].adc_ctrl = 0x80;

	/* bit 0 : adc module disable */
	IT83XX_ADC_ADCCFG &= ~0x01;
}

int adc_read_channel(enum adc_channel ch)
{
	/* voltage 0 ~ 3v = adc data register raw data 0 ~ 3FFh (10-bit ) */
	uint16_t adc_raw_data;
	int num;
	int adc_ch;

	adc_ch = adc_channels[ch].channel;

	adc_enable_channel(adc_ch);

	/* Maximum time for waiting ADC conversion is ~1.525ms */
	for (num = 0x00; num < 100; num++) {
		/* delay ~15.25us */
		IT83XX_GCTRL_WNCKR = 0;

		/* data valid of adc channel[x] */
		if (IT83XX_ADC_ADCDVSTS & (1 << adc_ch)) {
			/* read adc raw data msb and lsb */
			adc_raw_data = (*adc_ctrl_regs[adc_ch].adc_datm << 8) +
				*adc_ctrl_regs[adc_ch].adc_datl;

			/* W/C data valid flag */
			IT83XX_ADC_ADCDVSTS = (1 << adc_ch);

			adc_disable_channel(adc_ch);
			return adc_raw_data * adc_channels[ch].factor_mul /
				adc_channels[ch].factor_div +
				adc_channels[ch].shift;
		}
	}

	adc_disable_channel(adc_ch);
	return ADC_READ_ERROR;
}

int adc_read_all_channels(int *data)
{
	int index;

	for (index = 0; index < ADC_CH_COUNT; index++) {
		data[index] = adc_read_channel(index);
		if (data[index] == ADC_READ_ERROR)
			return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/*
 * ADC analog accuracy initialization (only once after VSTBY power on)
 *
 * Write 1 to this bit and write 0 to this bit immediately once and
 * only once during the firmware initialization and do not write 1 again
 * after initialization since IT8380 takes much power consumption
 * if this bit is set as 1
 */
static void adc_accuracy_initialization(void)
{
	/* bit3 : start adc accuracy initialization */
	IT83XX_ADC_ADCSTS |= 0x08;
	/* short delay for adc accuracy initialization */
	IT83XX_GCTRL_WNCKR = 0;
	/* bit3 : stop adc accuracy initialization */
	IT83XX_ADC_ADCSTS &= ~0x08;
}

/* ADC module Initialization */
static void adc_init(void)
{
	int index;
	int ch;

	/* ADC analog accuracy initialization */
	adc_accuracy_initialization();

	for (index = 0; index < ADC_CH_COUNT; index++) {
		ch = adc_channels[index].channel;

		/* enable adc channel[x] function pin */
		*adc_ctrl_regs[ch].adc_pin_ctrl = 0x00;
	}

	/* bit 5 : ADCCTS0 = 1 */
	IT83XX_ADC_ADCCFG = 0x20;

	IT83XX_ADC_ADCCTL = 0x04;
}
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_DEFAULT);
