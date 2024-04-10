/* Copyright 2013 The ChromiumOS Authors
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
} i2c_test_params[];

/* Disable I2C test for boards without test configuration */
#if defined(BOARD_BDS) || defined(BOARD_AURON)
#undef CONFIG_I2C
#endif

/* ADC test */
#define ADC_TEST_ITERATION 2000

/*****************************************************************************/
/* Test utilities */

/* period between 500us and 32ms */
#define RAND_US() (((prng_no_seed() % 64) + 1) * 500)

static int stress(const char *name, int (*test_routine)(void),
		  const int iteration)
{
	int i;

	for (i = 0; i < iteration; ++i) {
		if (i % 10 == 0) {
			ccprintf("\r%s...%d/%d", name, i, iteration);
			crec_usleep(RAND_US());
		}
		if (test_routine() != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;
	}

	ccprintf("\r%s...%d/%d\n", name, iteration, iteration);
	return EC_SUCCESS;
}

#define RUN_STRESS_TEST(n, r, iter)                     \
	do {                                            \
		if (stress(n, r, iter) != EC_SUCCESS) { \
			ccputs("Fail\n");               \
			error_count++;                  \
		}                                       \
	} while (0)

/*****************************************************************************/
/* Tests */
#ifdef CONFIG_I2C_CONTROLLER
static int test_i2c(void)
{
	int res = EC_ERROR_UNKNOWN;
	int mock_data;
	struct i2c_test_param_t *param;
	param = i2c_test_params +
		(prng_no_seed() %
		 (sizeof(i2c_test_params) / sizeof(struct i2c_test_param_t)));
	if (param->width == 8 && param->data == -1)
		res = i2c_read8(param->port, param->addr, param->offset,
				&mock_data);
	else if (param->width == 8 && param->data >= 0)
		res = i2c_write8(param->port, param->addr, param->offset,
				 param->data);
	else if (param->width == 16 && param->data == -1)
		res = i2c_read16(param->port, param->addr, param->offset,
				 &mock_data);
	else if (param->width == 16 && param->data >= 0)
		res = i2c_write16(param->port, param->addr, param->offset,
				  param->data);
	else if (param->width == 32 && param->data == -1)
		res = i2c_read32(param->port, param->addr, param->offset,
				 &mock_data);
	else if (param->width == 32 && param->data >= 0)
		res = i2c_write32(param->port, param->addr, param->offset,
				  param->data);

	return res;
}
#endif

#ifdef CONFIG_ADC
__attribute__((weak)) int adc_read_all_channels(int *data)
{
	int i;
	int rv = EC_SUCCESS;

	for (i = 0; i < ADC_CH_COUNT; ++i) {
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

void run_test(int argc, const char **argv)
{
	test_reset();

#ifdef CONFIG_I2C_CONTROLLER
	RUN_STRESS_TEST("I2C Stress Test", test_i2c, I2C_TEST_ITERATION);
#endif
#ifdef CONFIG_ADC
	RUN_STRESS_TEST("ADC Stress Test", test_adc, ADC_TEST_ITERATION);
#endif

	test_print_result();
}
