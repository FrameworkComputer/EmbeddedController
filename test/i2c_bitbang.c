/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "test_util.h"
#include "util.h"

const struct i2c_port_t i2c_bitbang_ports[] = {
	{"", 0, 100, GPIO_I2C_SCL, GPIO_I2C_SDA}
};
const unsigned int i2c_bitbang_ports_used = 1;

struct pin_state {
	int scl, sda;
} history[64];

int history_count;

void reset_state(void)
{
	history[0] = (struct pin_state) {1, 1};
	history_count = 1;
	bitbang_set_started(0);
}

void gpio_set_level(enum gpio_signal signal, int level)
{
	struct pin_state new = history[history_count - 1];

	/* reject if stack is full */
	if (history_count >= ARRAY_SIZE(history))
		return;

	if (signal == GPIO_I2C_SDA)
		new.sda = level;
	else if (signal == GPIO_I2C_SCL)
		new.scl = level;

	if (new.scl != history[history_count - 1].scl ||
			new.sda != history[history_count - 1].sda)
		history[history_count++] = new;
}

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_I2C_SDA)
		return history[history_count - 1].sda;
	else if (signal == GPIO_I2C_SCL)
		return history[history_count - 1].scl;

	return 0;
}

static int test_i2c_start_stop(void)
{
	struct pin_state expected[] = {
		/* start */
		{1, 1},
		{1, 0},
		{0, 0},
		/* stop */
		{1, 0},
		{1, 1},
	};
	int i;

	reset_state();

	bitbang_start_cond(&i2c_bitbang_ports[0]);
	bitbang_stop_cond(&i2c_bitbang_ports[0]);

	TEST_EQ((int)ARRAY_SIZE(expected), history_count, "%d");

	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		TEST_EQ(expected[i].scl, history[i].scl, "%d");
		TEST_EQ(expected[i].sda, history[i].sda, "%d");
	}

	return EC_SUCCESS;
}

static int test_i2c_repeated_start(void)
{
	struct pin_state expected[] = {
		/* start */
		{1, 1},
		{1, 0},
		{0, 0},
		/* repeated start */
		{0, 1},
		{1, 1},
		{1, 0},
		{0, 0},
	};
	int i;

	reset_state();

	bitbang_start_cond(&i2c_bitbang_ports[0]);
	bitbang_start_cond(&i2c_bitbang_ports[0]);

	TEST_EQ((int)ARRAY_SIZE(expected), history_count, "%d");

	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		TEST_EQ(expected[i].scl, history[i].scl, "%d");
		TEST_EQ(expected[i].sda, history[i].sda, "%d");
	}

	return EC_SUCCESS;
}

static int test_i2c_write(void)
{
	struct pin_state expected[] = {
		/* start */
		{1, 1},
		{1, 0},
		{0, 0},
		/* bit 7: 0 */
		{1, 0},
		{0, 0},
		/* bit 6: 1 */
		{0, 1},
		{1, 1},
		{0, 1},
		/* bit 5: 0 */
		{0, 0},
		{1, 0},
		{0, 0},
		/* bit 4: 1 */
		{0, 1},
		{1, 1},
		{0, 1},
		/* bit 3: 0 */
		{0, 0},
		{1, 0},
		{0, 0},
		/* bit 2: 1 */
		{0, 1},
		{1, 1},
		{0, 1},
		/* bit 1: 1 */
		{1, 1},
		{0, 1},
		/* bit 0: 0 */
		{0, 0},
		{1, 0},
		{0, 0},
		/* read bit */
		{0, 1},
		{1, 1},
		{0, 1},
		/* stop */
		{0, 0},
		{1, 0},
		{1, 1},
	};
	int i, ret;

	reset_state();

	bitbang_start_cond(&i2c_bitbang_ports[0]);
	ret = bitbang_write_byte(&i2c_bitbang_ports[0], 0x56);

	/* expected to fail because no slave answering the nack bit */
	TEST_EQ(EC_ERROR_BUSY, ret, "%d");

	TEST_EQ((int)ARRAY_SIZE(expected), history_count, "%d");

	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		TEST_EQ(expected[i].scl, history[i].scl, "%d");
		TEST_EQ(expected[i].sda, history[i].sda, "%d");
	}

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_i2c_start_stop);
	RUN_TEST(test_i2c_repeated_start);
	RUN_TEST(test_i2c_write);

	test_print_result();
}
