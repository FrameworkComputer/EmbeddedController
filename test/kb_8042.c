/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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

static int test_sysjump(void)
{
	set_scancode(2);
	enable_keystroke(1);

	system_run_image_copy(SYSTEM_IMAGE_RW);

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

void run_test(void)
{
	test_reset();

	if (system_get_image_copy() == SYSTEM_IMAGE_RO) {
		RUN_TEST(test_single_key_press);
		RUN_TEST(test_disable_keystroke);
		RUN_TEST(test_typematic);
		RUN_TEST(test_scancode_set2);
		RUN_TEST(test_sysjump);
	} else {
		RUN_TEST(test_sysjump_cont);
	}

	test_print_result();
}
