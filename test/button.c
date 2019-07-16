/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
#include "test_util.h"
#include "timer.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"

#define UNCHANGED -1

uint8_t keyboard_cols = KEYBOARD_COLS_MAX;

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

void run_test(void)
{
	test_reset();

	button_init();

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

	test_print_result();
}
