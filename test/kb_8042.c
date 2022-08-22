/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests for keyboard MKBP protocol
 */

#include "atkbd_protocol.h"
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
#include "queue.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static const char *action[2] = { "release", "press" };

struct to_host_data {
	uint8_t data;
	int irq;
};

/*
 * Keyboard or 8042 controller output to host.
 *
 * In the future we should have a separate keyboard queue and 8042 controller
 * queue so we don't lose keys while the keyboard port is inhibited.
 */
static struct queue const kbd_8042_ctrl_to_host =
	QUEUE_NULL(16, struct to_host_data);

static struct queue const aux_to_device = QUEUE_NULL(16, uint8_t);

static struct queue const aux_to_host = QUEUE_NULL(16, struct to_host_data);

/*****************************************************************************/
/* Mock functions */

int lid_is_open(void)
{
	return 1;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	struct to_host_data data = { .data = chr, .irq = send_irq };

	if (queue_add_unit(&kbd_8042_ctrl_to_host, &data) == 0)
		ccprintf("%s: ERROR: kbd_8042_ctrl_to_host queue is full!\n",
			 __FILE__);
}

void send_aux_data_to_device(uint8_t data)
{
	if (queue_add_unit(&aux_to_device, &data) == 0)
		ccprintf("%s: ERROR: aux_to_device queue is full!\n", __FILE__);
}

void lpc_aux_put_char(uint8_t chr, int send_irq)
{
	struct to_host_data data = { .data = chr, .irq = send_irq };

	if (queue_add_unit(&aux_to_host, &data) == 0)
		ccprintf("%s: ERROR: aux_to_host queue is full!\n", __FILE__);
}
/*****************************************************************************/
/* Test utilities */

/*
 * This is a bit tricky, the second parameter to _Static_assert must be a string
 * literal, so we use that property to assert x is a string literal.
 */
#define ASSERT_IS_STRING_LITERAL(x) _Static_assert(true, x)

#define VERIFY_LPC_CHAR_DELAY(s, d)                                       \
	do {                                                              \
		struct to_host_data _data;                                \
		const uint8_t *expected = s;                              \
		ASSERT_IS_STRING_LITERAL(s);                              \
		msleep(d);                                                \
		for (int _i = 0; _i < sizeof(s) - 1; ++_i) {              \
			TEST_EQ(queue_remove_unit(&kbd_8042_ctrl_to_host, \
						  &_data),                \
				(size_t)1, "%zd");                        \
			TEST_EQ(_data.data, expected[_i], "0x%x");        \
		}                                                         \
	} while (0)
#define VERIFY_LPC_CHAR(s) VERIFY_LPC_CHAR_DELAY(s, 30)
#define VERIFY_ATKBD_ACK(s) VERIFY_LPC_CHAR("\xfa") /* ATKBD_RET_ACK */

#define VERIFY_NO_CHAR()                                                  \
	do {                                                              \
		msleep(30);                                               \
		TEST_EQ(queue_is_empty(&kbd_8042_ctrl_to_host), 1, "%d"); \
	} while (0)

#define VERIFY_AUX_TO_HOST(expected_data, expected_irq)                    \
	do {                                                               \
		struct to_host_data data;                                  \
		msleep(30);                                                \
		TEST_EQ(queue_remove_unit(&aux_to_host, &data), (size_t)1, \
			"%zd");                                            \
		TEST_EQ(data.data, expected_data, "%#x");                  \
		TEST_EQ(data.irq, expected_irq, "%u");                     \
	} while (0)

#define VERIFY_AUX_TO_HOST_EMPTY()                         \
	do {                                               \
		msleep(30);                                \
		TEST_ASSERT(queue_is_empty(&aux_to_host)); \
	} while (0)

#define VERIFY_AUX_TO_DEVICE(expected_data)                                   \
	do {                                                                  \
		uint8_t _data;                                                \
		msleep(30);                                                   \
		TEST_EQ(queue_remove_unit(&aux_to_device, &_data), (size_t)1, \
			"%zd");                                               \
		TEST_EQ(_data, expected_data, "%#x");                         \
	} while (0)

#define VERIFY_AUX_TO_DEVICE_EMPTY()                         \
	do {                                                 \
		msleep(30);                                  \
		TEST_ASSERT(queue_is_empty(&aux_to_device)); \
	} while (0)

static void press_key(int c, int r, int pressed)
{
	ccprintf("Input %s (%d, %d)\n", action[pressed], c, r);
	keyboard_state_changed(r, c, pressed);
}

static int _enable_keystroke(int enabled)
{
	uint8_t data = enabled ? ATKBD_CMD_ENABLE : ATKBD_CMD_RESET_DIS;
	keyboard_host_write(data, 0);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}
#define ENABLE_KEYSTROKE(enabled) \
	TEST_EQ(_enable_keystroke(enabled), EC_SUCCESS, "%d")

static int _reset_8042(void)
{
	keyboard_host_write(ATKBD_CMD_RESET_DEF, 0);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}
#define RESET_8042() TEST_EQ(_reset_8042(), EC_SUCCESS, "%d")

static int _set_typematic(uint8_t val)
{
	keyboard_host_write(ATKBD_CMD_SETREP, 0);
	VERIFY_ATKBD_ACK();

	keyboard_host_write(val, 0);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}
#define SET_TYPEMATIC(val) TEST_EQ(_set_typematic(val), EC_SUCCESS, "%d")

static int _set_scancode(uint8_t s)
{
	keyboard_host_write(ATKBD_CMD_SSCANSET, 0);
	VERIFY_ATKBD_ACK();

	keyboard_host_write(s, 0);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}
#define SET_SCANCODE(s) TEST_EQ(_set_scancode(s), EC_SUCCESS, "%d")

static int _write_cmd_byte(uint8_t val)
{
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	VERIFY_NO_CHAR();

	keyboard_host_write(val, 0);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}
#define WRITE_CMD_BYTE(val) TEST_EQ(_write_cmd_byte(val), EC_SUCCESS, "%d")

static int _read_cmd_byte(uint8_t *cmd)
{
	struct to_host_data data;

	keyboard_host_write(I8042_READ_CMD_BYTE, 1);
	msleep(30);
	if (queue_remove_unit(&kbd_8042_ctrl_to_host, &data) == 0)
		return EC_ERROR_UNKNOWN;
	*cmd = data.data;

	return EC_SUCCESS;
}
#define READ_CMD_BYTE(cmd_ptr)                                   \
	({                                                       \
		uint8_t cmd;                                     \
		TEST_EQ(_read_cmd_byte(&cmd), EC_SUCCESS, "%d"); \
		cmd;                                             \
	})

/*
 * We unfortunately don't have an Input Buffer Full (IBF). Instead we
 * directly write to the task's input queue. Ideally we would have an
 * emulator that emulates the 8042 input/output buffers.
 */
#define i8042_write_cmd(cmd) keyboard_host_write(cmd, 1)
#define i8042_write_data(data) keyboard_host_write(data, 0)

/*****************************************************************************/
/* Tests */

void before_test(void)
{
	/* Make sure all tests start with the controller in the same state */
	_write_cmd_byte(I8042_XLATE | I8042_AUX_DIS | I8042_KBD_DIS);
}

void after_test(void)
{
	/* Hrmm, we can't fail the test here :( */

	if (!queue_is_empty(&aux_to_device))
		ccprintf("%s: ERROR: AUX to device queue is not empty!\n",
			 __FILE__);

	if (!queue_is_empty(&aux_to_host))
		ccprintf("%s: ERROR: AUX to host queue is not empty!\n",
			 __FILE__);
}

static int test_8042_aux_loopback(void)
{
	/* Disable all IRQs */
	WRITE_CMD_BYTE(0);

	i8042_write_cmd(I8042_ECHO_MOUSE);
	i8042_write_data(0x01);
	VERIFY_AUX_TO_HOST(0x01, 0);

	/* Enable AUX IRQ */
	WRITE_CMD_BYTE(I8042_ENIRQ12);

	i8042_write_cmd(I8042_ECHO_MOUSE);
	i8042_write_data(0x02);
	VERIFY_AUX_TO_HOST(0x02, 1);

	return EC_SUCCESS;
}

static int test_8042_aux_two_way_communication(void)
{
	/* Enable AUX IRQ */
	WRITE_CMD_BYTE(I8042_ENIRQ12);

	i8042_write_cmd(I8042_SEND_TO_MOUSE);
	i8042_write_data(0x01);
	/* No response expected from the 8042 controller*/
	VERIFY_AUX_TO_HOST_EMPTY();
	VERIFY_AUX_TO_DEVICE(0x01);

	/* Simulate the AUX device sending a response to the host */
	send_aux_data_to_host_interrupt(0x02);
	VERIFY_AUX_TO_HOST(0x02, 1);

	return EC_SUCCESS;
}

static int test_8042_aux_inhibit(void)
{
	/* Enable AUX IRQ, but inhibit the AUX device from sending data. */
	WRITE_CMD_BYTE(I8042_ENIRQ12 | I8042_AUX_DIS);

	/* Simulate the AUX device sending a response to the host */
	send_aux_data_to_host_interrupt(0x02);
	VERIFY_AUX_TO_HOST_EMPTY();

	/* Stop inhibiting the AUX device */
	WRITE_CMD_BYTE(I8042_ENIRQ12);
	/*
	 * This is wrong. When the CLK is inhibited the device will queue up
	 * events/scan codes in it's internal buffer. Once the inhibit is
	 * released, the device will start clocking out the data. So in this
	 * test we should be receiving a 0x02 byte, but we don't.
	 *
	 * In order to fix this we either need to plumb an inhibit function
	 * to the AUX PS/2 controller so it can hold the CLK line low, and thus
	 * tell the AUX device to buffer. Or, we can have the 8042 controller
	 * buffer the data internally and start replying it when the device is
	 * no longer inhibited.
	 */
	VERIFY_AUX_TO_HOST_EMPTY();

	return EC_SUCCESS;
}

static int test_8042_aux_controller_commands(void)
{
	uint8_t ctrl;

	/* Start with empty controller flags. i.e., AUX Enabled */
	WRITE_CMD_BYTE(0);

	/* Send the AUX DISABLE command and verify the ctrl got updated */
	i8042_write_cmd(I8042_DIS_MOUSE);
	ctrl = READ_CMD_BYTE();
	TEST_ASSERT(ctrl & I8042_AUX_DIS);

	/* Send the AUX ENABLE command and verify the ctrl got updated */
	i8042_write_cmd(I8042_ENA_MOUSE);
	ctrl = READ_CMD_BYTE();
	TEST_ASSERT(!(ctrl & I8042_AUX_DIS));

	return EC_SUCCESS;
}

static int test_8042_aux_test_command(void)
{
	/* Send the AUX DISABLE command and verify the ctrl got updated */
	i8042_write_cmd(I8042_TEST_MOUSE);

	VERIFY_LPC_CHAR("\x00");

	return EC_SUCCESS;
}

static int test_8042_keyboard_controller_commands(void)
{
	uint8_t ctrl;

	/* Start with empty controller flags. i.e., Keyboard Enabled */
	WRITE_CMD_BYTE(0);

	/* Send the Keyboard DISABLE command and verify the ctrl got updated */
	i8042_write_cmd(I8042_DIS_KB);
	ctrl = READ_CMD_BYTE();
	TEST_ASSERT(ctrl & I8042_KBD_DIS);

	/* Send the Keyboard ENABLE command and verify the ctrl got updated */
	i8042_write_cmd(I8042_ENA_KB);
	ctrl = READ_CMD_BYTE();
	TEST_ASSERT(!(ctrl & I8042_KBD_DIS));

	return EC_SUCCESS;
}

static int test_8042_keyboard_key_pressed_while_inhibited(void)
{
	ENABLE_KEYSTROKE(1);

	/* Inhibit the keyboard device from sending data. */
	WRITE_CMD_BYTE(I8042_XLATE | I8042_KBD_DIS);

	/* Simulate a keypress on the keyboard */
	press_key(1, 1, 1);

	/*
	 * FIXME: This is wrong! We shouldn't be sending scan codes to the
	 * host while the keyboard channel is inhibited. This should be
	 * VERIFY_NO_CHAR();
	 */
	VERIFY_LPC_CHAR("\x01");

	/* FIXME: This is also wrong for the same reason as above! */
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	/* Stop inhibiting the keyboard */
	WRITE_CMD_BYTE(0);

	/*
	 * FIXME: This is wrong. When the CLK is inhibited the device will queue
	 * up events/scan codes in it's internal buffer. Once the inhibit is
	 * released, the device will start clocking out the data. So in this
	 * test we should be receiving the 0x01, and x81 here, but we received
	 * them above.
	 */
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

static int test_single_key_press(void)
{
	ENABLE_KEYSTROKE(1);
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
	ENABLE_KEYSTROKE(0);
	press_key(1, 1, 1);
	VERIFY_NO_CHAR();
	press_key(1, 1, 0);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

static int test_typematic(void)
{
	ENABLE_KEYSTROKE(1);

	/*
	 * 250ms delay, 8 chars / sec.
	 */
	SET_TYPEMATIC(0xf);

	press_key(1, 1, 1);
	VERIFY_LPC_CHAR_DELAY("\x01\x01\x01\x01\x01", 650);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR_DELAY("\x81", 300);

	/*
	 * 500ms delay, 10.9 chars / sec.
	 */
	RESET_8042();

	press_key(1, 1, 1);
	VERIFY_LPC_CHAR_DELAY("\x01\x01\x01", 650);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR_DELAY("\x81", 200);

	return EC_SUCCESS;
}

static int test_scancode_set2(void)
{
	SET_SCANCODE(2);

	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x01");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	WRITE_CMD_BYTE(READ_CMD_BYTE() & ~I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x76");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\xf0\x76");

	return EC_SUCCESS;
}

static int test_power_button(void)
{
	ENABLE_KEYSTROKE(0);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	msleep(100);

	SET_SCANCODE(1);
	ENABLE_KEYSTROKE(1);
	test_chipset_on();

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	VERIFY_LPC_CHAR_DELAY("\xe0\x5e", 100);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	VERIFY_LPC_CHAR_DELAY("\xe0\xde", 100);

	SET_SCANCODE(2);
	WRITE_CMD_BYTE(READ_CMD_BYTE() & ~I8042_XLATE);

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
	SET_SCANCODE(2);
	ENABLE_KEYSTROKE(1);

	system_run_image_copy(EC_IMAGE_RW);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

static int test_sysjump_cont(void)
{
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	press_key(1, 1, 1);
	VERIFY_LPC_CHAR("\x01");
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x81");

	WRITE_CMD_BYTE(READ_CMD_BYTE() & ~I8042_XLATE);
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

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &keybd_config;
}

static int test_ec_cmd_get_keybd_config(void)
{
	struct ec_response_keybd_config resp;
	int rv;

	rv = test_send_host_command(EC_CMD_GET_KEYBD_CONFIG, 0, NULL, 0, &resp,
				    sizeof(resp));
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
	SET_SCANCODE(2);

	/* Test REFRESH key */
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	press_key(2, 3, 1); /* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x67"); /* Check REFRESH scancode in set-1 */

	/* Test SNAPSHOT key */
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	press_key(4, 3, 1); /* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x13"); /* Check SNAPSHOT scancode in set-1 */

	/* Test VOL_UP key */
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	press_key(5, 3, 1); /* Press T2 */
	VERIFY_LPC_CHAR("\xe0\x30"); /* Check VOL_UP scancode in set-1 */

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();
	wait_for_task_started();

	if (system_get_image_copy() == EC_IMAGE_RO) {
		RUN_TEST(test_8042_aux_loopback);
		RUN_TEST(test_8042_aux_two_way_communication);
		RUN_TEST(test_8042_aux_inhibit);
		RUN_TEST(test_8042_aux_controller_commands);
		RUN_TEST(test_8042_aux_test_command);
		RUN_TEST(test_8042_keyboard_controller_commands);
		RUN_TEST(test_8042_keyboard_key_pressed_while_inhibited);
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
