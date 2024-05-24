/* Copyright 2013 The ChromiumOS Authors
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
#include "keyboard_8042_sharedlib.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "power_button.h"
#include "queue.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <stdbool.h>

static const char *action[2] = { "release", "press" };

/*
 * This simulates the hardware output buffer. The x86 will read the output
 * buffer from IOx60. Since we don't have actual hardware, we emulate the
 * output buffer.
 */
static volatile struct {
	bool full;
	uint8_t data;
	bool irq;
	bool from_aux;
} output_buffer;

static struct queue const aux_to_device = QUEUE_NULL(16, uint8_t);

/*****************************************************************************/
/* Mock functions */

int lid_is_open(void)
{
	return 1;
}

int lpc_keyboard_has_char(void)
{
	return output_buffer.full;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	if (lpc_keyboard_has_char()) {
		ccprintf("%s:%s ERROR: output buffer is full!\n", __FILE__,
			 __func__);
		/* We can't fail the test, but we can abort!. */
		exit(1);
	}

	output_buffer.data = chr;
	output_buffer.irq = send_irq;
	output_buffer.from_aux = false;
	output_buffer.full = true;
}

void send_aux_data_to_device(uint8_t data)
{
	if (queue_add_unit(&aux_to_device, &data) == 0) {
		ccprintf("%s: ERROR: aux_to_device queue is full!\n", __FILE__);
		/* We can't fail the test, but we can abort!. */
		exit(1);
	}
}

void lpc_aux_put_char(uint8_t chr, int send_irq)
{
	if (lpc_keyboard_has_char()) {
		ccprintf("%s:%s ERROR: output buffer is full!\n", __FILE__,
			 __func__);
		/* We can't fail the test, but we can abort!. */
		exit(1);
	}

	output_buffer.data = chr;
	output_buffer.irq = send_irq;
	output_buffer.from_aux = true;
	output_buffer.full = true;
}
/*****************************************************************************/
/* Test utilities */

/*
 * This is a bit tricky, only string literal can concats with string literals
 * so we use that property to assert x is a string literal.
 */
#define ASSERT_IS_STRING_LITERAL(x) \
	_Static_assert(sizeof("" x "") == sizeof(x), "Not string literal")

int _wait_for_data(int delay_ms)
{
	while (!output_buffer.full) {
		if (delay_ms <= 0)
			break;
		delay_ms -= 1;

		crec_msleep(1);
	}
	TEST_ASSERT(output_buffer.full);

	return EC_SUCCESS;
}

#define WAIT_FOR_DATA(d) TEST_EQ(_wait_for_data(d), EC_SUCCESS, "%d")

#define VERIFY_LPC_CHAR_ALL(s, d, aux, _irq)                               \
	do {                                                               \
		const uint8_t *expected = s;                               \
		ASSERT_IS_STRING_LITERAL(s);                               \
		for (int _i = 0; _i < sizeof(s) - 1; ++_i) {               \
			WAIT_FOR_DATA(d);                                  \
			TEST_EQ(output_buffer.from_aux, aux, "%d");        \
			if (_irq >= 0)                                     \
				TEST_EQ(output_buffer.irq,                 \
					_irq > 0 ? true : false, "%d");    \
			TEST_EQ(output_buffer.data, expected[_i], "0x%x"); \
			output_buffer.full = false;                        \
			task_wake(TASK_ID_KEYPROTO);                       \
		}                                                          \
	} while (0)

#define VERIFY_LPC_CHAR_DELAY(s, d)                    \
	do {                                           \
		crec_msleep(d);                        \
		VERIFY_LPC_CHAR_ALL(s, 10, false, -1); \
	} while (0)
#define VERIFY_LPC_CHAR(s) VERIFY_LPC_CHAR_ALL(s, 30, false, -1)
#define VERIFY_ATKBD_ACK(s) VERIFY_LPC_CHAR("\xfa") /* ATKBD_RET_ACK */

#define VERIFY_NO_CHAR()                              \
	do {                                          \
		crec_msleep(30);                      \
		TEST_EQ(output_buffer.full, 0, "%d"); \
	} while (0)

#define VERIFY_AUX_TO_HOST(expected_data, expected_irq) \
	VERIFY_LPC_CHAR_ALL(expected_data, 30, true, expected_irq)

#define VERIFY_AUX_TO_HOST_EMPTY VERIFY_NO_CHAR

#define VERIFY_AUX_TO_DEVICE(expected_data)                                   \
	do {                                                                  \
		uint8_t _data;                                                \
		crec_msleep(30);                                              \
		TEST_EQ(queue_remove_unit(&aux_to_device, &_data), (size_t)1, \
			"%zd");                                               \
		TEST_EQ(_data, expected_data, "%#x");                         \
	} while (0)

#define VERIFY_AUX_TO_DEVICE_EMPTY()                         \
	do {                                                 \
		crec_msleep(30);                             \
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

static int _reset_8042_def(void)
{
	keyboard_host_write(ATKBD_CMD_RESET_DEF, 0);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}
#define RESET_8042_DEF() TEST_EQ(_reset_8042_def(), EC_SUCCESS, "%d")

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
	keyboard_host_write(I8042_READ_CMD_BYTE, 1);
	WAIT_FOR_DATA(30);
	TEST_EQ(output_buffer.from_aux, 0, "%d");

	*cmd = output_buffer.data;
	output_buffer.full = false;
	task_wake(TASK_ID_KEYPROTO);

	return EC_SUCCESS;
}
#define READ_CMD_BYTE()                                          \
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
	keyboard_clear_buffer();
	_write_cmd_byte(I8042_XLATE | I8042_AUX_DIS | I8042_KBD_DIS);
}

void after_test(void)
{
	if (output_buffer.full) {
		ccprintf("%s:%s ERROR: output buffer is not empty!\n", __FILE__,
			 __func__);
		/* We can't fail the test, but we can abort!. */
		exit(1);
	}
}

test_static int test_8042_aux_loopback(void)
{
	/* Disable all IRQs */
	WRITE_CMD_BYTE(0);

	i8042_write_cmd(I8042_ECHO_MOUSE);
	i8042_write_data(0x01);
	VERIFY_AUX_TO_HOST("\x01", 0);

	/* Enable AUX IRQ */
	WRITE_CMD_BYTE(I8042_ENIRQ12);

	i8042_write_cmd(I8042_ECHO_MOUSE);
	i8042_write_data(0x02);
	VERIFY_AUX_TO_HOST("\x02", 1);

	return EC_SUCCESS;
}

test_static int test_8042_aux_two_way_communication(void)
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
	VERIFY_AUX_TO_HOST("\x02", 1);

	return EC_SUCCESS;
}

test_static int test_8042_aux_inhibit(void)
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

test_static int test_8042_aux_controller_commands(void)
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

test_static int test_8042_aux_test_command(void)
{
	i8042_write_cmd(I8042_TEST_MOUSE);

	VERIFY_LPC_CHAR("\x00");

	return EC_SUCCESS;
}

test_static int test_8042_self_test(void)
{
	i8042_write_cmd(I8042_RESET_SELF_TEST);
	VERIFY_LPC_CHAR("\x55");

	return EC_SUCCESS;
}

test_static int test_8042_keyboard_test_command(void)
{
	i8042_write_cmd(I8042_TEST_KB_PORT);
	VERIFY_LPC_CHAR("\x00"); /* Data and Clock are not stuck */

	return EC_SUCCESS;
}

test_static int test_8042_keyboard_controller_commands(void)
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

test_static int test_8042_keyboard_key_pressed_while_inhibited(void)
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

test_static int
test_8042_keyboard_key_pressed_before_inhibit_using_cmd_byte(void)
{
	ENABLE_KEYSTROKE(1);
	/* Simulate a keypress on the keyboard */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/*
	 * We should have a press scan code in the output buffer, and a
	 * release scan code queued up in the keyboard queue.
	 */
	WAIT_FOR_DATA(30);

	/* Inhibit the keyboard device from sending data. */
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	keyboard_host_write(I8042_XLATE | I8042_KBD_DIS, 0);
	/* Wait for controller to processes the command */
	crec_msleep(10);

	/* Stop inhibiting the keyboard */
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	keyboard_host_write(I8042_XLATE, 0);
	/* Wait for controller to processes the command */
	crec_msleep(10);

	/* Verify the scan codes from above */
	VERIFY_LPC_CHAR("\x01");
	VERIFY_LPC_CHAR("\x81");

	return EC_SUCCESS;
}

test_static int
test_8042_keyboard_key_pressed_before_inhibit_using_cmd_byte_with_read(void)
{
	uint8_t cmd;

	ENABLE_KEYSTROKE(1);
	/* Simulate a keypress on the keyboard */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/*
	 * We should have a press scan code in the output buffer, and a
	 * release scan code queued up in the keyboard queue.
	 */
	WAIT_FOR_DATA(30);

	/* Inhibit the keyboard device from sending data. */
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	keyboard_host_write(I8042_XLATE | I8042_KBD_DIS, 0);
	/* Wait for controller to processes the command */
	crec_msleep(10);

	/* Read the key press scan code from the output buffer. */
	VERIFY_LPC_CHAR("\x01");

	/*
	 * With the keyboard output suppressed, we should be able to read from
	 * the 8042 controller.
	 */
	cmd = READ_CMD_BYTE();

	/* Verify we got the cmd byte we set above */
	TEST_EQ(cmd, I8042_XLATE | I8042_KBD_DIS, "%d");

	/* Stop inhibiting the keyboard */
	keyboard_host_write(I8042_WRITE_CMD_BYTE, 1);
	keyboard_host_write(I8042_XLATE, 0);
	/* Wait for controller to processes the command */
	crec_msleep(10);

	/* Verify the key release scan code from above */
	/*
	 * FIXME: This is wrong. We should receive the key release scan code
	 * 0x81. Instead the `I8042_READ_CMD_BYTE` above cleared the keyboard's
	 * output queue. It did this because the 8042 and keyboard output queues
	 * are implemented as the same thing.
	 */
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

test_static int test_8042_keyboard_key_pressed_before_inhibit_using_cmd(void)
{
	ENABLE_KEYSTROKE(1);
	/* Simulate a keypress on the keyboard */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/*
	 * We should have a press scan code in the output buffer, and a
	 * release scan code queued up in the keyboard queue.
	 */
	WAIT_FOR_DATA(30);

	/* Inhibit the keyboard device from sending data. */
	keyboard_host_write(I8042_DIS_KB, 1);

	/* Stop inhibiting the keyboard */
	keyboard_host_write(I8042_ENA_KB, 1);

	/* Verify the scan codes from above */
	VERIFY_LPC_CHAR("\x01");
	/*
	 * FIXME: This is wrong. When the keyboard CLK is inhibited the keyboard
	 * will queue up events/scan codes in it's internal buffer. Once the
	 * inhibit is released, the keyboard will start clocking out the data.
	 * So in this test we should be receiving 0x81, but the keyboard buffer
	 * was cleared by the I8042_DIS_KB above.
	 */
	VERIFY_NO_CHAR();
	return EC_SUCCESS;
}

test_static int test_single_key_press(void)
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

test_static int test_disable_keystroke(void)
{
	ENABLE_KEYSTROKE(0);
	press_key(1, 1, 1);
	VERIFY_NO_CHAR();
	press_key(1, 1, 0);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

test_static int test_typematic(void)
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
	RESET_8042_DEF();

	press_key(1, 1, 1);
	VERIFY_LPC_CHAR_DELAY("\x01\x01\x01", 650);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR_DELAY("\x81", 200);

	return EC_SUCCESS;
}

test_static int test_atkbd_get_scancode(void)
{
	SET_SCANCODE(1);

	keyboard_host_write(ATKBD_CMD_GSCANSET, 0);
	VERIFY_ATKBD_ACK();

	/* Writing a 0 scan code will return the current scan code. */
	keyboard_host_write(0, 0);
	VERIFY_ATKBD_ACK();
	VERIFY_LPC_CHAR("\x01");

	SET_SCANCODE(2);

	keyboard_host_write(ATKBD_CMD_GSCANSET, 0);
	VERIFY_ATKBD_ACK();

	/* Writing a 0 scan code will return the current scan code. */
	keyboard_host_write(0, 0);
	VERIFY_ATKBD_ACK();
	VERIFY_LPC_CHAR("\x02");

	return EC_SUCCESS;
}

test_static int test_atkbd_set_scancode_with_keystroke_disabled(void)
{
	ENABLE_KEYSTROKE(0);

	SET_SCANCODE(1);

	press_key(1, 1, 1);
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

test_static int test_atkbd_set_scancode_with_key_press_before_set(void)
{
	ENABLE_KEYSTROKE(0);
	ENABLE_KEYSTROKE(1);

	/* Push data into the output buffer and keyboard queue */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/*
	 * ATKBD_CMD_SSCANSET should cause the keyboard to stop scanning, flush
	 * the keyboards output queue, and reset the typematic key.
	 */
	i8042_write_data(ATKBD_CMD_SSCANSET);
	VERIFY_ATKBD_ACK();

	/*
	 * FIXME: This is wrong. The keyboard's output queue should have been
	 * flushed when it received the `ATKBD_CMD_SSCANSET` command.
	 */
	VERIFY_LPC_CHAR("\x01\x81");

	/* Finish setting scan code 1 */
	i8042_write_data(1);
	VERIFY_ATKBD_ACK();

	/* Key scanning should be restored. */
	press_key(1, 1, 1);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x01\x81");

	return EC_SUCCESS;
}

test_static int test_atkbd_set_scancode_with_key_press_during_set(void)
{
	ENABLE_KEYSTROKE(1);

	/*
	 * ATKBD_CMD_SSCANSET should cause the keyboard to stop scanning, flush
	 * the keyboards output queue, and reset the typematic key.
	 */
	i8042_write_data(ATKBD_CMD_SSCANSET);
	VERIFY_ATKBD_ACK();

	/* These keypresses should be dropped. */
	press_key(1, 1, 1);
	press_key(1, 1, 0);
	/*
	 * FIXME: So this is wrong. scanning should be stopped while waiting
	 * for the scan code to be sent.
	 */
	VERIFY_LPC_CHAR("\x01\x81");

	/* Finish setting scan code 1 */
	i8042_write_data(1);
	VERIFY_ATKBD_ACK();

	/* Key scanning should be restored. */
	press_key(1, 1, 1);
	press_key(1, 1, 0);
	VERIFY_LPC_CHAR("\x01\x81");

	return EC_SUCCESS;
}

test_static int test_atkbd_echo(void)
{
	i8042_write_data(ATKBD_CMD_DIAG_ECHO);
	VERIFY_ATKBD_ACK();

	VERIFY_LPC_CHAR("\xee");

	return EC_SUCCESS;
}

test_static int test_atkbd_get_id(void)
{
	i8042_write_data(ATKBD_CMD_GETID);
	VERIFY_ATKBD_ACK();

	VERIFY_LPC_CHAR("\xab\x83");

	i8042_write_data(ATKBD_CMD_OK_GETID);
	VERIFY_ATKBD_ACK();

	VERIFY_LPC_CHAR("\xab\x83");

	return EC_SUCCESS;
}

test_static int test_atkbd_set_leds_keypress_during(void)
{
	ENABLE_KEYSTROKE(1);

	/* This should pause scanning. */
	i8042_write_data(ATKBD_CMD_SETLEDS);
	VERIFY_ATKBD_ACK();

	/* Simulate keypress while keyboard is waiting for option byte */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/* Scancode is kept in queue during SETLEDS. */
	crec_msleep(15);
	TEST_EQ(output_buffer.full, 0, "%d");

	/* 2nd byte arrives (before timer expires) */
	i8042_write_data(0x01);
	VERIFY_ATKBD_ACK();

	/* Scancode previously queued should be sent now. */
	VERIFY_LPC_CHAR("\x01\x81");

	return EC_SUCCESS;
}

test_static int test_atkbd_set_leds_keypress_timeout(void)
{
	ENABLE_KEYSTROKE(1);

	/* This should pause scanning. */
	i8042_write_data(ATKBD_CMD_SETLEDS);
	VERIFY_ATKBD_ACK();

	/* Simulate keypress while keyboard is waiting for option byte */
	press_key(1, 1, 1);
	press_key(1, 1, 0);

	/* Scancode is kept in queue during SETLEDS. */
	crec_msleep(15);
	TEST_EQ(output_buffer.full, 0, "%d");

	/* Further wait until timer expires. */
	crec_msleep(15);

	/* Scancode previously queued should be sent now. */
	VERIFY_LPC_CHAR("\x01\x81");

	return EC_SUCCESS;
}

test_static int test_atkbd_set_leds_abort_set(void)
{
	i8042_write_data(ATKBD_CMD_SETLEDS);
	VERIFY_ATKBD_ACK();

	/*
	 * The spec says if we send a command instead of the option byte, the
	 * keyboard will abort the SETLEDS command and processes the new
	 * command. The way we can differentiate between a command and the
	 * option byte is the option byte must have the top 5 bits set to 0.
	 */
	i8042_write_data(ATKBD_CMD_DIAG_ECHO);
	VERIFY_ATKBD_ACK();

	/* FIXME: This is wrong. We are expecting the 0xee echo byte. */
	VERIFY_NO_CHAR();

	return EC_SUCCESS;
}

test_static int test_atkbd_set_ex_leds(void)
{
	i8042_write_data(ATKBD_CMD_EX_SETLEDS);
	VERIFY_ATKBD_ACK();

	/* The extra set led command expects two option bytes. */

	i8042_write_data(0x1);
	VERIFY_ATKBD_ACK();

	i8042_write_data(0x2);
	VERIFY_ATKBD_ACK();

	return EC_SUCCESS;
}

test_static int test_atkbd_reset(void)
{
	i8042_write_data(ATKBD_CMD_RESET);
	VERIFY_ATKBD_ACK();
	/* Successful BAT self-test */
	VERIFY_LPC_CHAR("\xAA");

	return EC_SUCCESS;
}

test_static int test_scancode_set2(void)
{
	SET_SCANCODE(2);
	ENABLE_KEYSTROKE(1);

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

test_static int test_power_button(void)
{
	ENABLE_KEYSTROKE(0);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	crec_msleep(100);

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

test_static int test_sysjump(void)
{
	SET_SCANCODE(2);
	ENABLE_KEYSTROKE(1);

	system_run_image_copy(EC_IMAGE_RW);

	/* Shouldn't reach here */
	return EC_ERROR_UNKNOWN;
}

test_static int test_sysjump_cont(void)
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

test_static const struct ec_response_keybd_config keybd_config = {
	.num_top_row_keys = 15,
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
		TK_ACCESSIBILITY,	/* T14 */
		TK_DICTATE,		/* T15 */
	},
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &keybd_config;
}

test_static int test_ec_cmd_get_keybd_config(void)
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

test_static int test_vivaldi_top_keys(void)
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

	/* Test ACCESSIBILITY key */
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	if (IS_ENABLED(CONFIG_FINCH))
		press_key(11, 0, 1); /* Press T14 */
	else
		press_key(9, 0, 1); /* Press T14 */
	VERIFY_LPC_CHAR("\xe0\x29"); /* Check ACCESSIBILITY scancode in set-1 */

	/* Test DICTATE key */
	WRITE_CMD_BYTE(READ_CMD_BYTE() | I8042_XLATE);
	if (IS_ENABLED(CONFIG_FINCH))
		press_key(12, 0, 1); /* Press T15 */
	else
		press_key(11, 0, 1); /* Press T15 */
	VERIFY_LPC_CHAR("\xe0\x27"); /* Check DICTATE scancode in set-1 */

	return EC_SUCCESS;
}

static scancode_set2_t scancode_test = {
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
};

extern scancode_set2_t *scancode_set2;

static int test_register_scancode_set2(void)
{
	/* Save */
	scancode_set2_t *scancode_default = scancode_set2;
	uint8_t cols = keyboard_get_cols();

	register_scancode_set2(&scancode_test, 1);
	TEST_ASSERT(keyboard_get_cols() == 1);
	TEST_ASSERT(scancode_set2 == &scancode_test);
	/* Out of bounds */
	TEST_ASSERT(get_scancode_set2(0, cols + 1) == 0);

	/* Restore */
	register_scancode_set2(scancode_default, cols);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	wait_for_task_started();

	if (system_get_image_copy() == EC_IMAGE_RO) {
		RUN_TEST(test_register_scancode_set2);
		RUN_TEST(test_8042_aux_loopback);
		RUN_TEST(test_8042_aux_two_way_communication);
		RUN_TEST(test_8042_aux_inhibit);
		RUN_TEST(test_8042_aux_controller_commands);
		RUN_TEST(test_8042_aux_test_command);
		RUN_TEST(test_8042_self_test);
		RUN_TEST(test_8042_keyboard_test_command);
		RUN_TEST(test_8042_keyboard_controller_commands);
		RUN_TEST(test_8042_keyboard_key_pressed_while_inhibited);
		RUN_TEST(
			test_8042_keyboard_key_pressed_before_inhibit_using_cmd_byte);
		RUN_TEST(
			test_8042_keyboard_key_pressed_before_inhibit_using_cmd_byte_with_read);
		RUN_TEST(
			test_8042_keyboard_key_pressed_before_inhibit_using_cmd);
		RUN_TEST(test_atkbd_get_scancode);
		RUN_TEST(test_atkbd_set_scancode_with_keystroke_disabled);
		RUN_TEST(test_atkbd_set_scancode_with_key_press_before_set);
		RUN_TEST(test_atkbd_set_scancode_with_key_press_during_set);
		RUN_TEST(test_atkbd_echo);
		RUN_TEST(test_atkbd_get_id);
		RUN_TEST(test_atkbd_set_leds_keypress_during);
		RUN_TEST(test_atkbd_set_leds_keypress_timeout);
		RUN_TEST(test_atkbd_set_leds_abort_set);
		RUN_TEST(test_atkbd_set_ex_leds);
		RUN_TEST(test_atkbd_reset);
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
