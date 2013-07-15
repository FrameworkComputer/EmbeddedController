/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Peripheral stress tests */

#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_ADC
#include "adc.h"
#endif

static int error_count;

/*****************************************************************************/
/* Test parameters */

/* I2C test */
#define I2C_TEST_ITERATION 2000

struct i2c_test_param_t {
	int width; /* 8 or 16 bits */
	int port;
	int addr;
	int offset;
	int data; /* Non-negative represents data to write. -1 to read. */
} i2c_test_params[] = {
#ifdef BOARD_spring
	{8, 0, 0x60, 0x0, -1},
	{8, 0, 0x60, 0x0, 0x40},
	{8, 0, 0x4a, 0x1, -1},
#elif defined(BOARD_daisy)
	{8, 1, 0x90, 0x19, -1},
#elif defined(BOARD_link)
	{8, 0, 0x16, 0x8, -1},
	{8, 0, 0x16, 0x9, -1},
	{8, 0, 0x16, 0xa, -1},
#elif defined(BOARD_pit)
	{8, 0, 0x90, 0x19, -1},
#elif defined(BOARD_snow)
	{8, 1, 0x90, 0x19, -1},
#endif
};
/* Disable I2C test for boards without test configuration */
#if defined(BOARD_bds) || defined(BOARD_mccroskey) || defined(BOARD_slippy) || \
	defined(BOARD_falco) || defined(BOARD_peppy) || defined(BOARD_wolf)
#undef CONFIG_I2C
#endif

/* ADC test */
#define ADC_TEST_ITERATION 2000

/* TODO(victoryang): PECI test */

/*****************************************************************************/
/* Test utilities */

/* Linear congruential pseudo random number generator*/
static uint32_t prng(void)
{
	static uint32_t x = 1357;
	x = 22695477 * x + 1;
	return x;
}

/* period between 500us and 32ms */
#define RAND_US() (((prng() % 64) + 1) * 500)

static int stress(const char *name,
		  int (*test_routine)(void),
		  const int iteration)
{
	int i;

	for (i = 0; i < iteration; ++i) {
		if (i % 10 == 0) {
			ccprintf("\r%s...%d/%d", name, i, iteration);
			usleep(RAND_US());
		}
		if (test_routine() != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;
	}

	ccprintf("\r%s...%d/%d\n", name, iteration, iteration);
	return EC_SUCCESS;
}

#define RUN_STRESS_TEST(n, r, iter) \
	do { \
		if (stress(n, r, iter) != EC_SUCCESS) { \
			ccputs("Fail\n"); \
			error_count++; \
		} \
	} while (0)

/*****************************************************************************/
/* Tests */
#ifdef CONFIG_I2C
static int test_i2c(void)
{
	int res = EC_ERROR_UNKNOWN;
	int dummy_data;
	struct i2c_test_param_t *param;
	param = i2c_test_params + (prng() % (sizeof(i2c_test_params) /
				   sizeof(struct i2c_test_param_t)));
	if (param->width == 8 && param->data == -1)
		res = i2c_read8(param->port, param->addr,
				param->offset, &dummy_data);
	else if (param->width == 8 && param->data >= 0)
		res = i2c_write8(param->port, param->addr,
				 param->offset, param->data);
	else if (param->width == 16 && param->data == -1)
		res = i2c_read16(param->port, param->addr,
				 param->offset, &dummy_data);
	else if (param->width == 16 && param->data >= 0)
		res = i2c_write16(param->port, param->addr,
				  param->offset, param->data);

	return res;
}
#endif

#ifdef CONFIG_ADC
__attribute__((weak)) int adc_read_all_channels(int *data)
{
	int i;
	int rv = EC_SUCCESS;

	for (i = 0 ; i < ADC_CH_COUNT; ++i) {
		data[i] = adc_read_channel(i);
		if (data[i] == ADC_READ_ERROR)
			rv = EC_ERROR_UNKNOWN;
	}

	return rv;
}

static int test_adc(void)
{
	int data[ADC_CH_COUNT];
	return adc_read_all_channels(data);
}
#endif

void run_test(void)
{
	test_reset();

#ifdef CONFIG_I2C
	RUN_STRESS_TEST("I2C Stress Test", test_i2c, I2C_TEST_ITERATION);
#endif
#ifdef CONFIG_ADC
	RUN_STRESS_TEST("ADC Stress Test", test_adc, ADC_TEST_ITERATION);
#endif

	test_print_result();
}
