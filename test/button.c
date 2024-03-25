/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test non-keyboard buttons.
 *
 * Using GPIOS and buttons[] defined in board/host/board.c
 * Volume down is active low with a debounce time of 30 mSec.
 * Volume up is active high with a debounce time of 60 mSec.
 *
 */

#include "button.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define UNCHANGED -1

static const struct button_config *button_vol_down =
	&buttons[BUTTON_VOLUME_DOWN];
static const struct button_config *button_vol_up = &buttons[BUTTON_VOLUME_UP];

static int button_state[BUTTON_COUNT];

/*
 * Callback from the button handling logic.
 * This is normally implemented by a keyboard protocol handler.
 */
void keyboard_update_button(enum keyboard_button_type button, int is_pressed)
{
	int i;

	for (i = 0; i < BUTTON_COUNT; i++) {
		if (buttons[i].type == button) {
			button_state[i] = is_pressed;
			break;
		}
	}
}

/* Test pressing a button */
static int test_button_press(void)
{
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(100);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);

	return EC_SUCCESS;
}

/* Test releasing a button */
static int test_button_release(void)
{
	gpio_set_level(button_vol_up->gpio, 0);
	msleep(100);
	gpio_set_level(button_vol_up->gpio, 1);
	msleep(100);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == 0);

	return EC_SUCCESS;
}

/* A press shorter than the debounce time should not trigger an update */
static int test_button_debounce_short_press(void)
{
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(10);
	gpio_set_level(button_vol_down->gpio, 1);
	msleep(100);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);

	return EC_SUCCESS;
}

/* A short bounce while pressing should still result in a button press */
static int test_button_debounce_short_bounce(void)
{
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(10);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	gpio_set_level(button_vol_down->gpio, 1);
	msleep(10);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);

	return EC_SUCCESS;
}

/* Button level must be stable for the entire debounce interval */
static int test_button_debounce_stability(void)
{
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	gpio_set_level(button_vol_down->gpio, 1);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	msleep(60);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	gpio_set_level(button_vol_down->gpio, 1);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 0);
	msleep(60);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 0);

	return EC_SUCCESS;
}

/* Test pressing both buttons at different times */
static int test_button_press_both(void)
{
	gpio_set_level(button_vol_down->gpio, 0);
	msleep(10);
	gpio_set_level(button_vol_up->gpio, 0);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == UNCHANGED);
	msleep(30);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == UNCHANGED);
	msleep(40);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == 1);

	return EC_SUCCESS;
}

/* Button simulate test cases */
static int send_button_hostcmd(uint32_t btn_mask, uint32_t press_ms)
{
	struct ec_params_button p;

	p.press_ms = press_ms;
	p.btn_mask = btn_mask;

	return test_send_host_command(EC_CMD_BUTTON, 0, &p, sizeof(p), NULL, 0);
}

static void test_sim_button_util(uint32_t btn_mask, uint32_t press_ms)
{
	send_button_hostcmd(btn_mask, press_ms);
	msleep(100);
}

/* Test simulate pressing a button */
static int test_sim_button_press(void)
{
	test_sim_button_util(1 << KEYBOARD_BUTTON_VOLUME_DOWN, 100);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);

	return EC_SUCCESS;
}

/* Test simulate releasing a button */
static int test_sim_button_release(void)
{
	test_sim_button_util(1 << KEYBOARD_BUTTON_VOLUME_UP, 50);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == 0);

	return EC_SUCCESS;
}

/* A press shorter than the debounce time should not trigger an update */
static int test_sim_button_debounce_short_press(void)
{
	test_sim_button_util(1 << KEYBOARD_BUTTON_VOLUME_DOWN, 10);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);

	return EC_SUCCESS;
}

/* A short bounce while pressing should still result in a button press */
static int test_sim_button_debounce_short_bounce(void)
{
	uint32_t btn_mask = 0;

	btn_mask |= (1 << KEYBOARD_BUTTON_VOLUME_DOWN);
	send_button_hostcmd(btn_mask, 10);
	msleep(50);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);

	send_button_hostcmd(btn_mask, 100);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);

	return EC_SUCCESS;
}

/* Button level must be stable for the entire debounce interval */
static int test_sim_button_debounce_stability(void)
{
	uint32_t btn_mask = 0;

	btn_mask |= (1 << KEYBOARD_BUTTON_VOLUME_DOWN);
	send_button_hostcmd(btn_mask, 10);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);

	send_button_hostcmd(btn_mask, 100);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	msleep(60);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);

	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	msleep(20);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 0);
	msleep(60);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 0);

	return EC_SUCCESS;
}

/* Test simulate pressing both buttons */
static int test_sim_button_press_both(void)
{
	uint32_t btn_mask = 0;

	btn_mask |= (1 << KEYBOARD_BUTTON_VOLUME_DOWN);
	btn_mask |= (1 << KEYBOARD_BUTTON_VOLUME_UP);
	send_button_hostcmd(btn_mask, 100);
	msleep(10);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == UNCHANGED);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == UNCHANGED);
	msleep(60);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 1);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == 1);
	msleep(100);
	TEST_ASSERT(button_state[BUTTON_VOLUME_DOWN] == 0);
	TEST_ASSERT(button_state[BUTTON_VOLUME_UP] == 0);

	return EC_SUCCESS;
}

static int test_button_init(void)
{
	TEST_ASSERT(button_get_boot_button() == 0);

	gpio_set_level(button_vol_down->gpio, 0);
	msleep(100);
	button_init();
	TEST_ASSERT(button_get_boot_button() == BIT(BUTTON_VOLUME_DOWN));

	return EC_SUCCESS;
}

static void button_test_init(void)
{
	int i;

	ccprints("Setting button GPIOs to inactive state");
	for (i = 0; i < BUTTON_COUNT; i++)
		gpio_set_level(buttons[i].gpio,
			       !(buttons[i].flags & BUTTON_FLAG_ACTIVE_HIGH));

	msleep(100);
	for (i = 0; i < BUTTON_COUNT; i++)
		button_state[i] = UNCHANGED;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	button_init();

	button_test_init();
	RUN_TEST(test_button_init);

	button_test_init();
	RUN_TEST(test_button_press);

	button_test_init();
	RUN_TEST(test_button_release);

	button_test_init();
	RUN_TEST(test_button_debounce_short_press);

	button_test_init();
	RUN_TEST(test_button_debounce_short_bounce);

	button_test_init();
	RUN_TEST(test_button_debounce_stability);

	button_test_init();
	RUN_TEST(test_button_press_both);

	button_test_init();
	RUN_TEST(test_sim_button_press);

	button_test_init();
	RUN_TEST(test_sim_button_release);

	button_test_init();
	RUN_TEST(test_sim_button_debounce_short_press);

	button_test_init();
	RUN_TEST(test_sim_button_debounce_short_bounce);

	button_test_init();
	RUN_TEST(test_sim_button_debounce_stability);

	button_test_init();
	RUN_TEST(test_sim_button_press_both);

	test_print_result();
}
