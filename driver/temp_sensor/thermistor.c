/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common thermistor code for Chrome EC */

#include "adc.h"
#include "common.h"
#include "gpio.h"
#include "thermistor.h"
#include "util.h"

int thermistor_linear_interpolate(uint16_t mv,
		const struct thermistor_info *info)
{
	const struct thermistor_data_pair *data = info->data;
	int v_high = 0, v_low = 0, t_low, t_high, num_steps;
	int head, tail, mid = 0;

	/* We need at least two points to form a line. */
	ASSERT(info->num_pairs >= 2);

	/*
	 * If input value is out of bounds return the lowest or highest
	 * value in the data sets provided.
	 */
	if (mv > data[0].mv * info->scaling_factor)
		return data[0].temp;
	else if (mv < data[info->num_pairs - 1].mv * info->scaling_factor)
		return data[info->num_pairs - 1].temp;

	head = 0;
	tail = info->num_pairs - 1;
	while (head != tail) {
		mid = (head + tail) / 2;
		v_high = data[mid].mv * info->scaling_factor;
		v_low = data[mid + 1].mv * info->scaling_factor;

		if ((mv <= v_high) && (mv >= v_low))
			break;
		else if (mv > v_high)
			tail = mid;
		else if (mv < v_low)
			head = mid + 1;
	}

	t_low = data[mid].temp;
	t_high = data[mid + 1].temp;

	/*
	 * The obvious way of doing this is to figure out how many mV per
	 * degree are in between the two points (mv_per_deg_c), and then how
	 * many of those exist between the input voltage and voltage of
	 * lower temperature :
	 *   1. mv_per_deg_c = (v_high - v_low) / (t_high - t_low)
	 *   2. num_steps = (v_high - mv) / mv_per_deg_c
	 *   3. result = t_low + num_steps
	 *
	 * Combine #1 and #2 to mitigate precision loss due to integer division.
	 */
	num_steps = ((v_high - mv) * (t_high - t_low)) / (v_high - v_low);
	return t_low + num_steps;
}

#if defined(CONFIG_STEINHART_HART_3V3_51K1_47K_4050B) || \
	defined(CONFIG_STEINHART_HART_3V3_13K7_47K_4050B) || \
	defined(CONFIG_STEINHART_HART_6V0_51K1_47K_4050B) || \
	defined(CONFIG_STEINHART_HART_3V0_22K6_47K_4050B) || \
	defined(CONFIG_STEINHART_HART_3V3_30K9_47K_4050B)
static int thermistor_get_temperature(int idx_adc, int *temp_ptr,
		const struct thermistor_info *info)
{
	int mv;

#ifdef CONFIG_TEMP_SENSOR_POWER_GPIO
	/*
	 * If the power rail for the thermistor circuit is not enabled, then
	 * need to ignore any ADC measurments.
	 */
	if (!gpio_get_level(CONFIG_TEMP_SENSOR_POWER_GPIO))
		return EC_ERROR_NOT_POWERED;
#endif /* CONFIG_TEMP_SENSOR_POWER_GPIO */
	mv = adc_read_channel(idx_adc);
	if (mv < 0)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = thermistor_linear_interpolate(mv, info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
/*
 * Data derived from Steinhart-Hart equation in a resistor divider circuit with
 * Vdd=3300mV, R = 51.1Kohm, and thermistor (B = 4050, T0 = 298.15 K, nominal
 * resistance (R0) = 47Kohm).
 */
#define THERMISTOR_SCALING_FACTOR_51_47 11
static const struct thermistor_data_pair thermistor_data_51_47[] = {
	{ 2512 / THERMISTOR_SCALING_FACTOR_51_47, 0   },
	{ 2158 / THERMISTOR_SCALING_FACTOR_51_47, 10  },
	{ 1772 / THERMISTOR_SCALING_FACTOR_51_47, 20  },
	{ 1398 / THERMISTOR_SCALING_FACTOR_51_47, 30  },
	{ 1070 / THERMISTOR_SCALING_FACTOR_51_47, 40  },
	{  803 / THERMISTOR_SCALING_FACTOR_51_47, 50  },
	{  597 / THERMISTOR_SCALING_FACTOR_51_47, 60  },
	{  443 / THERMISTOR_SCALING_FACTOR_51_47, 70  },
	{  329 / THERMISTOR_SCALING_FACTOR_51_47, 80  },
	{  285 / THERMISTOR_SCALING_FACTOR_51_47, 85  },
	{  247 / THERMISTOR_SCALING_FACTOR_51_47, 90  },
	{  214 / THERMISTOR_SCALING_FACTOR_51_47, 95  },
	{  187 / THERMISTOR_SCALING_FACTOR_51_47, 100 },
};

static const struct thermistor_info thermistor_info_51_47 = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR_51_47,
	.num_pairs = ARRAY_SIZE(thermistor_data_51_47),
	.data = thermistor_data_51_47,
};

int get_temp_3v3_51k1_47k_4050b(int idx_adc, int *temp_ptr)
{
	return thermistor_get_temperature(idx_adc, temp_ptr,
			&thermistor_info_51_47);
}
#endif /* CONFIG_STEINHART_HART_3V3_51K1_47K_4050B */

#ifdef CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
/*
 * Data derived from Steinhart-Hart equation in a resistor divider circuit with
 * Vdd=3300mV, R = 13.7Kohm, and thermistor (B = 4050, T0 = 298.15 K, nominal
 * resistance (R0) = 47Kohm).
 */
#define THERMISTOR_SCALING_FACTOR_13_47 13
static const struct thermistor_data_pair thermistor_data_13_47[] = {
	{ 3044 / THERMISTOR_SCALING_FACTOR_13_47, 0   },
	{ 2890 / THERMISTOR_SCALING_FACTOR_13_47, 10  },
	{ 2680 / THERMISTOR_SCALING_FACTOR_13_47, 20  },
	{ 2418 / THERMISTOR_SCALING_FACTOR_13_47, 30  },
	{ 2117 / THERMISTOR_SCALING_FACTOR_13_47, 40  },
	{ 1800 / THERMISTOR_SCALING_FACTOR_13_47, 50  },
	{ 1490 / THERMISTOR_SCALING_FACTOR_13_47, 60  },
	{ 1208 / THERMISTOR_SCALING_FACTOR_13_47, 70  },
	{  966 / THERMISTOR_SCALING_FACTOR_13_47, 80  },
	{  860 / THERMISTOR_SCALING_FACTOR_13_47, 85  },
	{  766 / THERMISTOR_SCALING_FACTOR_13_47, 90  },
	{  679 / THERMISTOR_SCALING_FACTOR_13_47, 95  },
	{  603 / THERMISTOR_SCALING_FACTOR_13_47, 100 },
};

static const struct thermistor_info thermistor_info_13_47 = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR_13_47,
	.num_pairs = ARRAY_SIZE(thermistor_data_13_47),
	.data = thermistor_data_13_47,
};

int get_temp_3v3_13k7_47k_4050b(int idx_adc, int *temp_ptr)
{
	return thermistor_get_temperature(idx_adc, temp_ptr,
			&thermistor_info_13_47);
}
#endif /* CONFIG_STEINHART_HART_3V3_13K7_47K_4050B */

#ifdef CONFIG_STEINHART_HART_6V0_51K1_47K_4050B
/*
 * Data derived from Steinhart-Hart equation in a resistor divider circuit with
 * Vdd=6000mV, R = 51.1Kohm, and thermistor (B = 4050, T0 = 298.15 K, nominal
 * resistance (R0) = 47Kohm).
 */
#define THERMISTOR_SCALING_FACTOR_6V0_51_47 18
static const struct thermistor_data_pair thermistor_data_6v0_51_47[] = {
	{ 4517 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 0   },
	{ 3895 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 10  },
	{ 3214 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 20  },
	{ 2546 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 30  },
	{ 1950 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 40  },
	{ 1459 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 50  },
	{ 1079 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 60  },
	{  794 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 70  },
	{  584 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 80  },
	{  502 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 85  },
	{  432 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 90  },
	{  372 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 95  },
	{  322 / THERMISTOR_SCALING_FACTOR_6V0_51_47, 100 },
};

static const struct thermistor_info thermistor_info_6v0_51_47 = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR_6V0_51_47,
	.num_pairs = ARRAY_SIZE(thermistor_data_6v0_51_47),
	.data = thermistor_data_6v0_51_47,
};

int get_temp_6v0_51k1_47k_4050b(int idx_adc, int *temp_ptr)
{
	return thermistor_get_temperature(idx_adc, temp_ptr,
			&thermistor_info_6v0_51_47);
}
#endif /* CONFIG_STEINHART_HART_6V0_51K1_47K_4050B */

#ifdef CONFIG_STEINHART_HART_3V0_22K6_47K_4050B
/*
 * Data derived from Steinhart-Hart equation in a resistor divider circuit with
 * Vdd=3000mV, R = 22.6Kohm, and thermistor (B = 4050, T0 = 298.15 K, nominal
 * resistance (R0) = 47Kohm).
 */
#define THERMISTOR_SCALING_FACTOR_22_47 11
static const struct thermistor_data_pair thermistor_data_22_47[] = {
	{ 2625 / THERMISTOR_SCALING_FACTOR_22_47, 0   },
	{ 2425 / THERMISTOR_SCALING_FACTOR_22_47, 10  },
	{ 2170 / THERMISTOR_SCALING_FACTOR_22_47, 20  },
	{ 1875 / THERMISTOR_SCALING_FACTOR_22_47, 30  },
	{ 1563 / THERMISTOR_SCALING_FACTOR_22_47, 40  },
	{ 1263 / THERMISTOR_SCALING_FACTOR_22_47, 50  },
	{  995 / THERMISTOR_SCALING_FACTOR_22_47, 60  },
	{  770 / THERMISTOR_SCALING_FACTOR_22_47, 70  },
	{  589 / THERMISTOR_SCALING_FACTOR_22_47, 80  },
	{  514 / THERMISTOR_SCALING_FACTOR_22_47, 85  },
	{  448 / THERMISTOR_SCALING_FACTOR_22_47, 90  },
	{  391 / THERMISTOR_SCALING_FACTOR_22_47, 95  },
	{  341 / THERMISTOR_SCALING_FACTOR_22_47, 100 },
};

static const struct thermistor_info thermistor_info_22_47 = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR_22_47,
	.num_pairs = ARRAY_SIZE(thermistor_data_22_47),
	.data = thermistor_data_22_47,
};

int get_temp_3v0_22k6_47k_4050b(int idx_adc, int *temp_ptr)
{
	return thermistor_get_temperature(idx_adc, temp_ptr,
			&thermistor_info_22_47);
}
#endif /* CONFIG_STEINHART_HART_3V0_22K6_47K_4050B */

#ifdef CONFIG_STEINHART_HART_3V3_30K9_47K_4050B
/*
 * Data derived from Steinhart-Hart equation in a resistor divider circuit with
 * Vdd=3000mV, R = 30.9Kohm, and thermistor (B = 4050, T0 = 298.15 K, nominal
 * resistance (R0) = 47Kohm).
 */
#define THERMISTOR_SCALING_FACTOR_31_47 11
static const struct thermistor_data_pair thermistor_data_31_47[] = {
	{ 2753 / THERMISTOR_SCALING_FACTOR_31_47, 0   },
	{ 2487 / THERMISTOR_SCALING_FACTOR_31_47, 10  },
	{ 2165 / THERMISTOR_SCALING_FACTOR_31_47, 20  },
	{ 1813 / THERMISTOR_SCALING_FACTOR_31_47, 30  },
	{ 1145 / THERMISTOR_SCALING_FACTOR_31_47, 50  },
	{  878 / THERMISTOR_SCALING_FACTOR_31_47, 60  },
	{  665 / THERMISTOR_SCALING_FACTOR_31_47, 70  },
	{  500 / THERMISTOR_SCALING_FACTOR_31_47, 80  },
	{  375 / THERMISTOR_SCALING_FACTOR_31_47, 90  },
	{  282 / THERMISTOR_SCALING_FACTOR_31_47, 100 },
};

static const struct thermistor_info thermistor_info_31_47 = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR_31_47,
	.num_pairs = ARRAY_SIZE(thermistor_data_31_47),
	.data = thermistor_data_31_47,
};

int get_temp_3v3_30k9_47k_4050b(int idx_adc, int *temp_ptr)
{
	return thermistor_get_temperature(idx_adc, temp_ptr,
			&thermistor_info_31_47);
}
#endif /* CONFIG_STEINHART_HART_3V3_30K9_47K_4050B */
