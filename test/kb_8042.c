/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for keyboard MKBP protocol
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "i8042_protocol.h"
#include "keyboard_8042.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "power_button.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static const char *action[2] = {"release", "press"};

#define BUF_SIZE 16
static char lpc_char_buf[BUF_SIZE];
static unsigned int lpc_char_cnt;

/*****************************************************************************/
/* Mock functions */

int lid_is_open(void)
{
	return 1;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	lpc_char_buf[lpc_char_cnt++] = chr;
}

/*****************************************************************************/
/* Test utilities */

static void press_key(int c, int r, int pressed)
{
	ccprintf("Input %s (%d, %d)\n", action[pressed], c, r);
	keyboard_state_changed(r, c, pressed);
}

static void enable_keystroke(int enabled)
{
	uint8_t data = enabled ? I8042_CMD_ENABLE : I8042_CMD_RESET_DIS;
	keyboard_host_write(data, 0);
	msleep(30);
}

static void reset_8042(void)
{
	keyboard_host_write(I8042_CMD_RESET_DEF, 0);
	msleep(30);
}

static void set_typematic(uint8_t val)
{
	keyboard_host_write(I8042_CMD_SETREP, 0);
	msleep(30);
	keyboard_host_write(val, 0);
	msleep(30);
}

static void set_scancode(uint8_t s)
{
	keyboard_host_write(I8042_CMD_SSCANSET, 0);
	msleep(30);
	keyboard_host_write(s, 0);
	msleep(30);
}

static void write_cmd_byte(uint8_t val)
{
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	msleep(30);
	keyboard_host_write(val, 0);
	msleep(30);
}

static uint8_t read_cmd_byte(void)
{
	lpc_char_cnt = 0;
	keyboard_host_write(I8042_READ_CMD_BYTE, 1);
	msleep(30);
	return lpc_char_buf[0];
}

static int __verify_lpc_char(char *arr, unsigned int sz, int delay_ms)
{
	int i;

	lpc_char_cnt = 0;
	for (i = 0; i < sz; ++i)
		lpc_char_buf[i] = 0;
	msleep(delay_ms);
	TEST_ASSERT_ARRAY_EQ(arr, lpc_char_buf, sz);
	return EC_SUCCESS;
}

#define VERIFY_LPC_CHAR(s) \
	TEST_ASSERT(__verify_lpc_char(s, strlen(s), 30) == EC_SUCCESS)
#define VERIFY_LPC_CHAR_DELAY(s, t) \
	TEST_ASSERT(__verify_lpc_char(s, strlen(s), t) == EC_SUCCESS)

static int __verify_no_char(void)
{
	lpc_char_cnt = 0;
	msleep(30);
	TEST_CHECK(lpc_char_cnt == 0);
}

#define VERIFY_NO_CHAR() TEST_ASSERT(__verify_no_char() == EC_SUCCESS)

/*****************************************************************************/
/* Tests */

static int test_single_key_press(void)
{
	enable_keystroke(1);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x01");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	press_key(12, 6, 1);
	VERIFY_LPC_CHAR("\xe0\x4d");
	press_key(12, 6, 0);
	VERIFY_LPC_CHAR("\xe0\xcd");

	return EC_SUCCESS;
}

static int test_disable_keystroke(void)
{
	enable_keystroke(0);
	press_key(1, 1, 1);
	VERIFY_NO_CHAR();
	press_key(1, 1, 0);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

static int test_typematic(void)
{
	enable_keystroke(1);

	/*
	 * 250ms delay, 8 chars / sec.
	 */
	set_typematic(0xf);

	press_key(1, 1, 1);
	VERIFY_LPC_CHAR_DELAY("\x01\x01\x01\x01\x01", 650);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR_DELAY("\x81", 300);

	/*
	 * 500ms delay, 10.9 chars / sec.
	 */
	reset_8042();

	press_key(1, 1, 1);
	VERIFY_LPC_CHAR_DELAY("\x01\x01\x01", 650);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR_DELAY("\x81", 200);

	return EC_SUCCESS;
}

static int test_scancode_set2(void)
{
	set_scancode(2);

	write_cmd_byte(read_cmd_byte() | I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x01");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	write_cmd_byte(read_cmd_byte() & ~I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x76");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\xf0\x76");

	return EC_SUCCESS;
}

static int test_power_button(void)
{
	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	set_scancode(1);
	test_chipset_on();

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	VERIFY_LPC_CHAR_DELAY("\xe0\x5e", 100);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	VERIFY_LPC_CHAR_DELAY("\xe0\xde", 100);

	set_scancode(2);
	write_cmd_byte(read_cmd_byte() & ~I8042_XLATE);

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	VERIFY_LPC_CHAR_DELAY("\xe0\x37", 100);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	VERIFY_LPC_CHAR_DELAY("\xe0\xf0\x37", 100);

	test_chipset_off();

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	VERIFY_NO_CHAR();

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

static int test_sysjump(void)
{
	set_scancode(2);
	enable_keystroke(1);

	system_run_image_copy(EC_IMAGE_RW);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

static int test_sysjump_cont(void)
{
	write_cmd_byte(read_cmd_byte() | I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x01");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	write_cmd_byte(read_cmd_byte() & ~I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x76");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\xf0\x76");

	return EC_SUCCESS;
}

static const struct ec_response_keybd_config keybd_config = {
	.num_top_row_keys = 13,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_KBD_BKLIGHT_DOWN,	/* T8 */
		TK_KBD_BKLIGHT_UP,	/* T9 */
		TK_PLAY_PAUSE,		/* T10 */
		TK_VOL_MUTE,		/* T11 */
		TK_VOL_DOWN,		/* T12 */
		TK_VOL_UP,		/* T13 */
	},
};

__override const struct ec_response_keybd_config
*board_vivaldi_keybd_config(void)
{
	return &keybd_config;
}

static int test_ec_cmd_get_keybd_config(void)
{
	struct ec_response_keybd_config resp;
	int rv;

	rv = test_send_host_command(EC_CMD_GET_KEYBD_CONFIG, 0, NULL, 0,
				    &resp, sizeof(resp));
	if (rv != EC_RES_SUCCESS) {
		ccprintf("Error: EC_CMD_GET_KEYBD_CONFIG cmd returns %d\n", rv);
		return EC_ERROR_INVAL;
	}

	if (memcmp(&resp, &keybd_config, sizeof(resp))) {
		ccprintf("Error: EC_CMD_GET_KEYBD_CONFIG returned bad cfg\n");
		return EC_ERROR_INVAL;
	}

	ccprintf("EC_CMD_GET_KEYBD_CONFIG response is good\n");
	return EC_SUCCESS;
}

static int test_vivaldi_top_keys(void)
{
	set_scancode(2);

	/* Test REFRESH key */
	write_cmd_byte(read_cmd_byte() | I8042_XLATE);
	press_key(2, 3, 1);		/* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x67");	/* Check REFRESH scancode in set-1 */

	/* Test SNAPSHOT key */
	write_cmd_byte(read_cmd_byte() | I8042_XLATE);
	press_key(4, 3, 1);		/* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x13");	/* Check SNAPSHOT scancode in set-1 */

	/* Test VOL_UP key */
	write_cmd_byte(read_cmd_byte() | I8042_XLATE);
	press_key(5, 3, 1);		/* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x30");	/* Check VOL_UP scancode in set-1 */

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();
	wait_for_task_started();

	if (system_get_image_copy() == EC_IMAGE_RO) {
		RUN_TEST(test_single_key_press);
		RUN_TEST(test_disable_keystroke);
		RUN_TEST(test_typematic);
		RUN_TEST(test_scancode_set2);
		RUN_TEST(test_power_button);
		RUN_TEST(test_ec_cmd_get_keybd_config);
		RUN_TEST(test_vivaldi_top_keys);
		RUN_TEST(test_sysjump);
	} else {
		RUN_TEST(test_sysjump_cont);
	}

	test_print_result();
}
