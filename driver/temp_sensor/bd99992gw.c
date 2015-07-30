/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * BD99992GW PMIC temperature sensor module for Chrome EC.
 * Note that ADC / temperature sensor registers are only active while
 * the PMIC is in S0.
 */

#include "bd99992gw.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "temp_sensor.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

/* List of active channels, ordered by pointer register */
static enum bd99992gw_adc_channel
	active_channels[BD99992GW_ADC_POINTER_REG_COUNT];

/*
 * Use 27ms as the period between ADC conversions, as we will typically be
 * sampling temperature sensors every second, and 27ms is the longest
 * supported period.
 */
#define ADC_LOOP_PERIOD BD99992GW_ADC1CNTL1_SLP27MS

/*
 * ADC-to-temp conversion assumes recommended thermistor / resistor
 * configuration specified in datasheet (NCP15WB* / 24.9K).
 * For 50C through 100C, use linear interpolation from discreet points
 * in table below. For temps < 50C, use a simplified linear function.
 */
#define ADC_DISCREET_RANGE_START_TEMP	50
/* 10 bit ADC result corresponding to START_TEMP */
#define ADC_DISCREET_RANGE_START_RESULT	407

#define ADC_DISCREET_RANGE_LIMIT_TEMP	100
/* 10 bit ADC result corresponding to LIMIT_TEMP */
#define ADC_DISCREET_RANGE_LIMIT_RESULT	107

/* Table entries in steppings of 5C */
#define ADC_DISCREET_RANGE_STEP		5

/* Discreet range ADC results (9 bit) per temperature, in 5 degree steps */
static const uint8_t adc_result[] = {
	203,	/* 50 C */
	178,	/* 55 C */
	157,	/* 60 C */
	138,	/* 65 C */
	121,	/* 70 C */
	106,	/* 75 C */
	93,	/* 80 C */
	81,	/* 85 C */
	70,	/* 90 C */
	61,	/* 95 C */
	53,	/* 100 C */
};

/*
 * From 20C (reasonable lower limit of temperatures we care about accuracy)
 * to 50C, the temperature curve is roughly linear, so we don't need to include
 * data points in our table.
 */
#define adc_to_temp(result) (ADC_DISCREET_RANGE_START_TEMP - \
	(((result) - ADC_DISCREET_RANGE_START_RESULT) * 3 + 16) / 32)

static int raw_read8(const int offset, int *data_ptr)
{
	int ret;
	ret = i2c_read8(I2C_PORT_THERMAL, BD99992GW_I2C_ADDR, offset, data_ptr);
	if (ret != EC_SUCCESS)
		CPRINTS("bd99992gw read fail %d\n", ret);
	return ret;
}

static int raw_write8(const int offset, int data)
{
	int ret;
	ret = i2c_write8(I2C_PORT_THERMAL, BD99992GW_I2C_ADDR, offset, data);
	if (ret != EC_SUCCESS)
		CPRINTS("bd99992gw write fail %d\n", ret);
	return ret;
}

static void bd99992gw_init(void)
{
	int i;
	int active_channel_count = 0;
	uint8_t pointer_reg = BD99992GW_REG_ADC1ADDR0;

	/* Mark active channels from the board temp sensor table */
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i)
		if (temp_sensors[i].read == bd99992gw_get_val)
			active_channels[active_channel_count++] =
				temp_sensors[i].idx;

	/* Make sure we don't have too many active channels. */
	ASSERT(active_channel_count <= ARRAY_SIZE(active_channels));

	/* Mark the first unused channel so we know where to stop searching */
	if (active_channel_count != ARRAY_SIZE(active_channels))
		active_channels[active_channel_count] =
			BD99992GW_ADC_CHANNEL_NONE;

	/* Now write pointer regs with channel to monitor */
	for (i = 0; i < active_channel_count; ++i)
		/* Write stop bit on last channel */
		if (raw_write8(pointer_reg + i, active_channels[i] |
			  ((i == active_channel_count - 1) ?
			  BD99992GW_ADC1ADDR_STOP : 0)))
			return;

	/* Enable ADC interrupts */
	if (raw_write8(BD99992GW_REG_MADC1INT, 0xf & ~BD99992GW_MADC1INT_RND))
		return;
	if (raw_write8(BD99992GW_REG_IRQLVL1MSK, BD99992GW_IRQLVL1MSK_MADC))
		return;

	/* Enable ADC sequencing */
	if (raw_write8(BD99992GW_REG_ADC1CNTL2, BD99992GW_ADC1CNTL2_ADCTHERM))
		return;

	/* Start round-robin conversions at 27ms period */
	raw_write8(BD99992GW_REG_ADC1CNTL1, ADC_LOOP_PERIOD |
		   BD99992GW_ADC1CNTL1_ADEN | BD99992GW_ADC1CNTL1_ADSTRT);
}
/*
 * Some regs only work in S0, so we must initialize on AP startup in
 * addition to INIT.
 */
DECLARE_HOOK(HOOK_INIT, bd99992gw_init, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, bd99992gw_init, HOOK_PRIO_DEFAULT);

/* Convert ADC result to temperature in celsius */
test_export_static int bd99992gw_get_temp(uint16_t adc)
{
	int temp;
	int head, tail, mid;
	uint8_t delta, step;

	/* Is ADC result in linear range? */
	if (adc >= ADC_DISCREET_RANGE_START_RESULT) {
		temp = adc_to_temp(adc);
	}
	/* Hotter than our discreet range limit? */
	else if (adc <= ADC_DISCREET_RANGE_LIMIT_RESULT) {
		temp = ADC_DISCREET_RANGE_LIMIT_TEMP;
	}
	/* We're in the discreet range */
	else {
		/* Table uses 9 bit ADC values */
		adc /= 2;

		/* Binary search to find proper table entry */
		head = 0;
		tail = ARRAY_SIZE(adc_result) - 1;
		while (head != tail) {
			mid = (head + tail) / 2;
			if (adc_result[mid] >= adc &&
			    adc_result[mid+1] < adc)
				break;
			if (adc_result[mid] > adc)
				head = mid + 1;
			else
				tail = mid;
		}

		/* Now fit between table entries using linear interpolation. */
		if (head != tail) {
			delta = adc_result[mid] - adc_result[mid + 1];
			step = ((adc_result[mid] - adc) *
				ADC_DISCREET_RANGE_STEP + delta / 2) / delta;
		} else {
			/* Edge case where adc = max */
			mid = head;
			step = 0;
		}

		temp = ADC_DISCREET_RANGE_START_TEMP +
		       ADC_DISCREET_RANGE_STEP * mid + step;
	}

	return temp;
}

/* Get temperature from requested sensor */
int bd99992gw_get_val(int idx, int *temp_ptr)
{
	uint16_t adc;
	int i, read, ret;
	enum bd99992gw_adc_channel channel;

	/* ADC unit is only functional in S0 */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_NOT_POWERED;

	/* Find requested channel */
	for (i = 0; i < ARRAY_SIZE(active_channels); ++i) {
		channel = active_channels[i];
		if (channel == idx ||
		    channel == BD99992GW_ADC_CHANNEL_NONE)
			break;
	}

	/* Make sure we found it */
	if (i == ARRAY_SIZE(active_channels) ||
	    active_channels[i] != idx) {
		CPRINTS("Bad ADC channel %d\n", idx);
		return EC_ERROR_INVAL;
	}

	/* Pause conversions */
	ret = raw_write8(0x80,
			 ADC_LOOP_PERIOD |
			 BD99992GW_ADC1CNTL1_ADEN |
			 BD99992GW_ADC1CNTL1_ADSTRT |
			 BD99992GW_ADC1CNTL1_ADPAUSE);
	if (ret)
		return ret;

	/* Read 10-bit ADC result */
	ret = raw_read8(BD99992GW_REG_ADC1DATA0L + 2 * i, &read);
	if (ret)
		return ret;
	adc = read;
	ret = raw_read8(BD99992GW_REG_ADC1DATA0H + 2 * i, &read);
	if (ret)
		return ret;
	adc |= read << 2;

	/* Convert temperature to C / K */
	*temp_ptr = C_TO_K(bd99992gw_get_temp(adc));

	/* Clear interrupts */
	ret = raw_write8(BD99992GW_REG_ADC1INT, BD99992GW_ADC1INT_RND);
	if (ret)
		return ret;
	ret = raw_write8(BD99992GW_REG_IRQLVL1, BD99992GW_IRQLVL1_ADC);
	if (ret)
		return ret;

	/* Resume conversions */
	ret = raw_write8(BD99992GW_REG_ADC1CNTL1, ADC_LOOP_PERIOD |
		   BD99992GW_ADC1CNTL1_ADEN | BD99992GW_ADC1CNTL1_ADSTRT);
	if (ret)
		return ret;

	return EC_SUCCESS;
}
