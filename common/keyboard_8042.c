/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 8042 keyboard protocol
 */

#include "chipset.h"
#include "button.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "i8042_protocol.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "lightbar.h"
#include "lpc.h"
#include "power_button.h"
#include "queue.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

#ifdef CONFIG_KEYBOARD_DEBUG
#define CPUTS5(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTF5(format, args...) cprintf(CC_KEYBOARD, format, ## args)
#else
#define CPUTS5(outstr)
#define CPRINTF5(format, args...)
#endif

static enum {
	STATE_NORMAL = 0,
	STATE_SCANCODE,
	STATE_SETLEDS,
	STATE_EX_SETLEDS_1,		/* Expect 2-byte parameter */
	STATE_EX_SETLEDS_2,
	STATE_WRITE_CMD_BYTE,
	STATE_WRITE_OUTPUT_PORT,
	STATE_ECHO_MOUSE,
	STATE_SETREP,
	STATE_SEND_TO_MOUSE,
} data_port_state = STATE_NORMAL;

enum scancode_set_list {
	SCANCODE_GET_SET = 0,
	SCANCODE_SET_1,
	SCANCODE_SET_2,
	SCANCODE_SET_3,
	SCANCODE_MAX = SCANCODE_SET_3,
};

#define MAX_SCAN_CODE_LEN 4

/*
 * Mutex to control write access to the to-host buffer head.  Don't need to
 * mutex the tail because reads are only done in one place.
 */
static struct mutex to_host_mutex;

static uint8_t to_host_buffer[16];
static struct queue to_host = {
	.buf_bytes  = sizeof(to_host_buffer),
	.unit_bytes = sizeof(uint8_t),
	.buf        = to_host_buffer,
};

/* Queue command/data from the host */
enum {
	HOST_COMMAND = 0,
	HOST_DATA,
};
struct host_byte {
	uint8_t type;
	uint8_t byte;
};

/*
 * The buffer for i8042 command from host. So far the largest command
 * we see from kernel is:
 *
 *   d1 -> i8042 (command)    # enable A20 in i8042_platform_init() of
 *   df -> i8042 (parameter)  # serio/i8042-x86ia64io.h file.
 *   ff -> i8042 (command)
 *   20 -> i8042 (command)    # read CTR
 *
 * Hence, 5 (actually 4 plus one spare) is large enough, but use 8 for safety.
 */
static uint8_t from_host_buffer[8 * sizeof(struct host_byte)];
static struct queue from_host = {
	.buf_bytes  = sizeof(from_host_buffer),
	.unit_bytes = sizeof(struct host_byte),
	.buf        = from_host_buffer,
};

static int i8042_irq_enabled;

/* i8042 global settings */
static int keyboard_enabled;	/* default the keyboard is disabled. */
static int keystroke_enabled;	/* output keystrokes */
static uint8_t resend_command[MAX_SCAN_CODE_LEN];
static uint8_t resend_command_len;
static uint8_t controller_ram_address;
static uint8_t controller_ram[0x20] = {
	/* the so called "command byte" */
	I8042_XLATE | I8042_AUX_DIS | I8042_KBD_DIS,
	/* 0x01 - 0x1f are controller RAM */
};
static uint8_t A20_status;
static void keyboard_special(uint16_t k);

/*
 * Scancode settings
 */
static enum scancode_set_list scancode_set = SCANCODE_SET_2;

/*
 * Typematic delay, rate and counter variables.
 *
 *    7     6     5     4     3     2     1     0
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |un-  |   delay   |     B     |        D        |
 * | used|  0     1  |  0     1  |  0     1     1  |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * Formula:
 *   the inter-char delay = (2 ** B) * (D + 8) / 240 (sec)
 * Default: 500ms delay, 10.9 chars/sec.
 */
#define DEFAULT_TYPEMATIC_VALUE ((1 << 5) | (1 << 3) | (3 << 0))
static uint8_t typematic_value_from_host;
static int typematic_first_delay;
static int typematic_inter_delay;
static int typematic_len;  /* length of typematic_scan_code */
static uint8_t typematic_scan_code[MAX_SCAN_CODE_LEN];
static timestamp_t typematic_deadline;

#define KB_SYSJUMP_TAG 0x4b42  /* "KB" */
#define KB_HOOK_VERSION 2
/* the previous keyboard state before reboot_ec. */
struct kb_state {
	uint8_t codeset;
	uint8_t ctlram;
	uint8_t keystroke_enabled;
};

/* The standard Chrome OS keyboard matrix table. */
static const uint16_t scancode_set1[KEYBOARD_ROWS][KEYBOARD_COLS] = {
	{0x0000, 0xe05b, 0x003b, 0x0030, 0x0044, 0x0073, 0x0031, 0x0000, 0x000d,
	 0x0000, 0xe038, 0x0000, 0x0000},
	{0x0000, 0x0001, 0x003e, 0x0022, 0x0041, 0x0000, 0x0023, 0x0000, 0x0028,
	 0x0043, 0x0000, 0x000e, 0x0079},
	{0x001d, 0x000f, 0x003d, 0x0014, 0x0040, 0x001b, 0x0015, 0x0056, 0x001a,
	 0x0042, 0x007d, 0x0000, 0x0000},
	{0x0000, 0x0029, 0x003c, 0x0006, 0x003f, 0x0000, 0x0007, 0x0000, 0x000c,
	 0x005d, 0x0000, 0x002b, 0x007b},
	{0xe01d, 0x001e, 0x0020, 0x0021, 0x001f, 0x0025, 0x0024, 0x0000, 0x0027,
	 0x0026, 0x002b, 0x001c, 0x0000},
	{0x0000, 0x002c, 0x002e, 0x002f, 0x002d, 0x0033, 0x0032, 0x002a, 0x0035,
	 0x0034, 0x0000, 0x0039, 0x0000},
	{0x0000, 0x0002, 0x0004, 0x0005, 0x0003, 0x0009, 0x0008, 0x0000, 0x000b,
	 0x000a, 0x0038, 0xe050, 0xe04d},
	{0x0000, 0x0010, 0x0012, 0x0013, 0x0011, 0x0017, 0x0016, 0x0036, 0x0019,
	 0x0018, 0x0000, 0xe048, 0xe04b},
};

static const uint16_t scancode_set2[KEYBOARD_ROWS][KEYBOARD_COLS] = {
	{0x0000, 0xe01f, 0x0005, 0x0032, 0x0009, 0x0051, 0x0031, 0x0000, 0x0055,
	 0x0000, 0xe011, 0x0000, 0x0000},
	{0x0000, 0x0076, 0x000c, 0x0034, 0x0083, 0x0000, 0x0033, 0x0000, 0x0052,
	 0x0001, 0x0000, 0x0066, 0x0064},
	{0x0014, 0x000d, 0x0004, 0x002c, 0x000b, 0x005b, 0x0035, 0x0061, 0x0054,
	 0x000a, 0x006a, 0x0000, 0x0000},
	{0x0000, 0x000e, 0x0006, 0x002e, 0x0003, 0x0000, 0x0036, 0x0000, 0x004e,
	 0x002f, 0x0000, 0x005d, 0x0067},
	{0xe014, 0x001c, 0x0023, 0x002b, 0x001b, 0x0042, 0x003b, 0x0000, 0x004c,
	 0x004b, 0x005d, 0x005a, 0x0000},
	{0x0000, 0x001a, 0x0021, 0x002a, 0x0022, 0x0041, 0x003a, 0x0012, 0x004a,
	 0x0049, 0x0000, 0x0029, 0x0000},
	{0x0000, 0x0016, 0x0026, 0x0025, 0x001e, 0x003e, 0x003d, 0x0000, 0x0045,
	 0x0046, 0x0011, 0xe072, 0xe074},
	{0x0000, 0x0015, 0x0024, 0x002d, 0x001d, 0x0043, 0x003c, 0x0059, 0x004d,
	 0x0044, 0x0000, 0xe075, 0xe06b},
};

/* Button scancodes. Must be in the same order as defined in button_type */
static const uint16_t button_scancodes[2][KEYBOARD_BUTTON_COUNT] = {
	/* Set 1 */
	{0xe05e, 0xe02e, 0xe030},
	/* Set 2 */
	{0xe037, 0xe021, 0xe033},
};
BUILD_ASSERT(ARRAY_SIZE(button_scancodes[0]) == KEYBOARD_BUTTON_COUNT);

/*****************************************************************************/
/* Keyboard event log */

/* Log the traffic between EC and host -- for debug only */
#define MAX_KBLOG 512  /* Max events in keyboard log */

struct kblog_t {
	/*
	 * Type:
	 *
	 * s = byte enqueued to send to host
	 * t = to-host queue tail pointer before type='s' bytes enqueued
	 *
	 * d = data byte from host
	 * c = command byte from host
	 *
	 * k = to-host queue head pointer before byte dequeued
	 * K = byte actually sent to host via LPC
	 */
	uint8_t type;
	uint8_t byte;
};

static struct kblog_t *kblog_buf;	/* Log buffer; NULL if not logging */
static int kblog_len;			/* Current log length */

/**
 * Add event to keyboard log.
 */
static void kblog_put(char type, uint8_t byte)
{
	if (kblog_buf && kblog_len < MAX_KBLOG) {
		kblog_buf[kblog_len].type = type;
		kblog_buf[kblog_len].byte = byte;
		kblog_len++;
	}
}

/*****************************************************************************/

void keyboard_host_write(int data, int is_cmd)
{
	struct host_byte h;

	h.type = is_cmd ? HOST_COMMAND : HOST_DATA;
	h.byte = data;
	queue_add_units(&from_host, &h, 1);
	task_wake(TASK_ID_KEYPROTO);
}

/**
 * Enable keyboard IRQ generation.
 *
 * @param enable	Enable (!=0) or disable (0) IRQ generation.
 */
static void keyboard_enable_irq(int enable)
{
	i8042_irq_enabled = enable;
	if (enable)
		lpc_keyboard_resume_irq();
}

/**
 * Send a scan code to the host.
 *
 * The EC lib will push the scan code bytes to host via port 0x60 and assert
 * the IBF flag to trigger an interrupt.  The EC lib must queue them if the
 * host cannot read the previous byte away in time.
 *
 * @param len		Number of bytes to send to the host
 * @param to_host	Data to send
 */
static void i8042_send_to_host(int len, const uint8_t *bytes)
{
	int i;

	for (i = 0; i < len; i++)
		kblog_put('s', bytes[i]);

	/* Enqueue output data if there's space */
	mutex_lock(&to_host_mutex);
	if (queue_has_space(&to_host, len)) {
		kblog_put('t', to_host.tail);
		queue_add_units(&to_host, bytes, len);
	}
	mutex_unlock(&to_host_mutex);

	/* Wake up the task to move from queue to host */
	task_wake(TASK_ID_KEYPROTO);
}

/* Change to set 1 if the I8042_XLATE flag is set. */
static enum scancode_set_list acting_code_set(enum scancode_set_list set)
{
	/* Always generate set 1 if keyboard translation is enabled */
	if (controller_ram[0] & I8042_XLATE)
		return SCANCODE_SET_1;

	return set;
}

/**
 * Return the make or break code bytes for the active scancode set.
 *
 * @param make_code	The make code to generate the make or break code from
 * @param pressed	Whether the key or button was pressed
 * @param code_set	The scancode set being used
 * @param scan_code	An array of bytes to store the make or break code in
 * @param len		The number of valid bytes to send in scan_code
 */
static void scancode_bytes(uint16_t make_code, int8_t pressed,
			   enum scancode_set_list code_set, uint8_t *scan_code,
			   int32_t *len)
{
	*len = 0;

	/* Output the make code (from table) */
	if (make_code >= 0x0100) {
		*len += 2;
		scan_code[0] = make_code >> 8;
		scan_code[1] = make_code & 0xff;
	} else {
		*len += 1;
		scan_code[0] = make_code & 0xff;
	}

	switch (code_set) {
	case SCANCODE_SET_1:
		/* OR 0x80 for the last byte. */
		if (!pressed) {
			ASSERT(*len >= 1);
			scan_code[*len - 1] |= 0x80;
		}
		break;

	case SCANCODE_SET_2:
		/*
		 * Insert the break byte, move back the last byte and insert a
		 * 0xf0 byte before that.
		 */
		if (!pressed) {
			ASSERT(*len >= 1);
			scan_code[*len] = scan_code[*len - 1];
			scan_code[*len - 1] = 0xf0;
			*len += 1;
		}
		break;
	default:
		break;
	}
}

static enum ec_error_list matrix_callback(int8_t row, int8_t col,
					  int8_t pressed,
					  enum scancode_set_list code_set,
					  uint8_t *scan_code, int32_t *len)
{
	uint16_t make_code;

	ASSERT(scan_code);
	ASSERT(len);

	if (row > KEYBOARD_ROWS || col > KEYBOARD_COLS)
		return EC_ERROR_INVAL;

	if (pressed)
		keyboard_special(scancode_set1[row][col]);

	code_set = acting_code_set(code_set);

	switch (code_set) {
	case SCANCODE_SET_1:
		make_code = scancode_set1[row][col];
		break;

	case SCANCODE_SET_2:
		make_code = scancode_set2[row][col];
		break;

	default:
		CPRINTF("[%T KB scancode set %d unsupported]\n", code_set);
		return EC_ERROR_UNIMPLEMENTED;
	}

	if (!make_code) {
		CPRINTF("[%T KB scancode %d:%d missing]\n", row, col);
		return EC_ERROR_UNIMPLEMENTED;
	}

	scancode_bytes(make_code, pressed, code_set, scan_code, len);
	return EC_SUCCESS;
}

/**
 * Set typematic delays based on host data byte.
 */
static void set_typematic_delays(uint8_t data)
{
	typematic_value_from_host = data;
	typematic_first_delay = MSEC *
		(((typematic_value_from_host & 0x60) >> 5) + 1) * 250;
	typematic_inter_delay = SECOND *
		(1 << ((typematic_value_from_host & 0x18) >> 3)) *
		((typematic_value_from_host & 0x7) + 8) / 240;
}

static void reset_rate_and_delay(void)
{
	set_typematic_delays(DEFAULT_TYPEMATIC_VALUE);
}

void keyboard_clear_buffer(void)
{
	mutex_lock(&to_host_mutex);
	queue_reset(&to_host);
	mutex_unlock(&to_host_mutex);
	lpc_keyboard_clear_buffer();
}

static void keyboard_wakeup(void)
{
	host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
}

void keyboard_state_changed(int row, int col, int is_pressed)
{
	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	int32_t len;
	enum ec_error_list ret;

	CPRINTF5("[%T KB (%d,%d)=%d]\n", row, col, is_pressed);

	ret = matrix_callback(row, col, is_pressed, scancode_set, scan_code,
			      &len);
	if (ret == EC_SUCCESS) {
		ASSERT(len > 0);
		if (keystroke_enabled)
			i8042_send_to_host(len, scan_code);
	}

	if (is_pressed) {
		keyboard_wakeup();

		typematic_deadline.val = get_time().val + typematic_first_delay;

		memcpy(typematic_scan_code, scan_code, len);
		typematic_len = len;
		task_wake(TASK_ID_KEYPROTO);
	} else {
		typematic_len = 0;
	}
}

static void keystroke_enable(int enable)
{
	if (!keystroke_enabled && enable)
		CPRINTF("[%T KS enable]\n");
	else if (keystroke_enabled && !enable)
		CPRINTF("[%T KS disable]\n");

	keystroke_enabled = enable;
}

static void keyboard_enable(int enable)
{
	if (!keyboard_enabled && enable) {
		CPRINTF("[%T KB enable]\n");
	} else if (keyboard_enabled && !enable) {
		CPRINTF("[%T KB disable]\n");
		reset_rate_and_delay();
		typematic_len = 0;  /* stop typematic */

		/* Disable keystroke as well in case the BIOS doesn't
		 * disable keystroke where repeated strokes are queued
		 * before kernel initializes keyboard. Hence the kernel
		 * is unable to get stable CTR read (get key codes
		 * instead).
		 */
		keystroke_enable(0);
		keyboard_clear_buffer();
	}
	keyboard_enabled = enable;
}

static uint8_t read_ctl_ram(uint8_t addr)
{
	if (addr < ARRAY_SIZE(controller_ram))
		return controller_ram[addr];
	else
		return 0;
}

/**
 * Manipulate the controller_ram[].
 *
 * Some bits change may trigger internal state change.
 */
static void update_ctl_ram(uint8_t addr, uint8_t data)
{
	uint8_t orig;

	if (addr >= ARRAY_SIZE(controller_ram))
		return;

	orig = controller_ram[addr];
	controller_ram[addr] = data;
	CPRINTF5("[%T KB set CTR_RAM(0x%02x)=0x%02x (old:0x%02x)]\n",
		 addr, data, orig);

	if (addr == 0x00) {
		/* Keyboard enable/disable */

		/* Enable IRQ before enable keyboard (queue chars to host) */
		if (!(orig & I8042_ENIRQ1) && (data & I8042_ENIRQ1))
			keyboard_enable_irq(1);

		/* Handle the I8042_KBD_DIS bit */
		keyboard_enable(!(data & I8042_KBD_DIS));

		/*
		 * Disable IRQ after disable keyboard so that every char must
		 * have informed the host.
		 */
		if ((orig & I8042_ENIRQ1) && !(data & I8042_ENIRQ1))
			keyboard_enable_irq(0);
	}
}

/**
 * Handle the port 0x60 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 */
static int handle_keyboard_data(uint8_t data, uint8_t *output)
{
	int out_len = 0;
	int save_for_resend = 1;
	int i;

	CPRINTF5("[%T KB recv data: 0x%02x]\n", data);
	kblog_put('d', data);

	switch (data_port_state) {
	case STATE_SCANCODE:
		CPRINTF5("[%T KB eaten by STATE_SCANCODE: 0x%02x]\n", data);
		if (data == SCANCODE_GET_SET) {
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = scancode_set;
		} else {
			scancode_set = data;
			CPRINTF("[%T KB scancode set to %d]\n", scancode_set);
			output[out_len++] = I8042_RET_ACK;
		}
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SETLEDS:
		CPUTS5("[%T KB eaten by STATE_SETLEDS]\n");
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_EX_SETLEDS_1:
		CPUTS5("[%T KB eaten by STATE_EX_SETLEDS_1]\n");
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_EX_SETLEDS_2;
		break;

	case STATE_EX_SETLEDS_2:
		CPUTS5("[%T KB eaten by STATE_EX_SETLEDS_2]\n");
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_WRITE_CMD_BYTE:
		CPRINTF5("[%T KB eaten by STATE_WRITE_CMD_BYTE: 0x%02x]\n",
			 data);
		update_ctl_ram(controller_ram_address, data);
		data_port_state = STATE_NORMAL;
		break;

	case STATE_WRITE_OUTPUT_PORT:
		CPRINTF5("[%T KB eaten by STATE_WRITE_OUTPUT_PORT: 0x%02x]\n",
			 data);
		A20_status = (data & (1 << 1)) ? 1 : 0;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_ECHO_MOUSE:
		CPRINTF5("[%T KB eaten by STATE_ECHO_MOUSE: 0x%02x]\n", data);
		output[out_len++] = data;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SETREP:
		CPRINTF5("[%T KB eaten by STATE_SETREP: 0x%02x]\n", data);
		set_typematic_delays(data);

		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SEND_TO_MOUSE:
		CPRINTF5("[%T KB eaten by STATE_SEND_TO_MOUSE: 0x%02x]\n",
			 data);
		data_port_state = STATE_NORMAL;
		break;

	default:  /* STATE_NORMAL */
		switch (data) {
		case I8042_CMD_GSCANSET:  /* also I8042_CMD_SSCANSET */
			output[out_len++] = I8042_RET_ACK;
			data_port_state = STATE_SCANCODE;
			break;

		case I8042_CMD_SETLEDS:
			/* Chrome OS doesn't have keyboard LEDs, so ignore */
			output[out_len++] = I8042_RET_ACK;
			data_port_state = STATE_SETLEDS;
			break;

		case I8042_CMD_EX_SETLEDS:
			output[out_len++] = I8042_RET_ACK;
			data_port_state = STATE_EX_SETLEDS_1;
			break;

		case I8042_CMD_DIAG_ECHO:
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = I8042_CMD_DIAG_ECHO;
			break;

		case I8042_CMD_GETID:    /* fall-thru */
		case I8042_CMD_OK_GETID:
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = 0xab;  /* Regular keyboards */
			output[out_len++] = 0x83;
			break;

		case I8042_CMD_SETREP:
			output[out_len++] = I8042_RET_ACK;
			data_port_state = STATE_SETREP;
			break;

		case I8042_CMD_ENABLE:
			output[out_len++] = I8042_RET_ACK;
			keystroke_enable(1);
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET_DIS:
			output[out_len++] = I8042_RET_ACK;
			keystroke_enable(0);
			reset_rate_and_delay();
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET_DEF:
			output[out_len++] = I8042_RET_ACK;
			reset_rate_and_delay();
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET_BAT:
			reset_rate_and_delay();
			keyboard_clear_buffer();
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = I8042_RET_BAT;
			output[out_len++] = I8042_RET_BAT;
			break;

		case I8042_CMD_RESEND:
			save_for_resend = 0;
			for (i = 0; i < resend_command_len; ++i)
				output[out_len++] = resend_command[i];
			break;

		case 0x60: /* fall-thru */
		case 0x45:
			/* U-boot hack.  Just ignore; don't reply. */
			break;

		case I8042_CMD_SETALL_MB:  /* fall-thru */
		case I8042_CMD_SETALL_MBR:
		case I8042_CMD_EX_ENABLE:
		default:
			output[out_len++] = I8042_RET_NAK;
			CPRINTF("[%T KB Unsupported i8042 data 0x%02x]\n",
				data);
			break;
		}
	}

	/* For resend, keep output before leaving. */
	if (out_len && save_for_resend) {
		ASSERT(out_len <= MAX_SCAN_CODE_LEN);
		for (i = 0; i < out_len; ++i)
			resend_command[i] = output[i];
		resend_command_len = out_len;
	}

	ASSERT(out_len <= MAX_SCAN_CODE_LEN);
	return out_len;
}

/**
 * Handle the port 0x64 writes from host.
 *
 * This functions returns the number of bytes stored in *output buffer.
 * BUT those bytes will appear at port 0x60.
 */
static int handle_keyboard_command(uint8_t command, uint8_t *output)
{
	int out_len = 0;

	CPRINTF5("[%T KB recv cmd: 0x%02x]\n", command);
	kblog_put('c', command);

	switch (command) {
	case I8042_READ_CMD_BYTE:
		output[out_len++] = read_ctl_ram(0);
		break;

	case I8042_WRITE_CMD_BYTE:
		data_port_state = STATE_WRITE_CMD_BYTE;
		controller_ram_address = command - 0x60;
		break;

	case I8042_DIS_KB:
		update_ctl_ram(0, read_ctl_ram(0) | I8042_KBD_DIS);
		break;

	case I8042_ENA_KB:
		update_ctl_ram(0, read_ctl_ram(0) & ~I8042_KBD_DIS);
		break;

	case I8042_READ_OUTPUT_PORT:
		output[out_len++] =
			(lpc_keyboard_input_pending() ? (1 << 5) : 0) |
			(lpc_keyboard_has_char() ? (1 << 4) : 0) |
			(A20_status ? (1 << 1) : 0) |
			1;  /* Main processor in normal mode */
		break;

	case I8042_WRITE_OUTPUT_PORT:
		data_port_state = STATE_WRITE_OUTPUT_PORT;
		break;

	case I8042_RESET_SELF_TEST:
		output[out_len++] = 0x55;  /* Self test success */
		break;

	case I8042_TEST_KB_PORT:
		output[out_len++] = 0x00;
		break;

	case I8042_DIS_MOUSE:
		update_ctl_ram(0, read_ctl_ram(0) | I8042_AUX_DIS);
		break;

	case I8042_ENA_MOUSE:
		update_ctl_ram(0, read_ctl_ram(0) & ~I8042_AUX_DIS);
		break;

	case I8042_TEST_MOUSE:
		output[out_len++] = 0;  /* No error detected */
		break;

	case I8042_ECHO_MOUSE:
		data_port_state = STATE_ECHO_MOUSE;
		break;

	case I8042_SEND_TO_MOUSE:
		data_port_state = STATE_SEND_TO_MOUSE;
		break;

	case I8042_SYSTEM_RESET:
		chipset_reset(0);
		break;

	default:
		if (command >= I8042_READ_CTL_RAM &&
		    command <= I8042_READ_CTL_RAM_END) {
			output[out_len++] = read_ctl_ram(command - 0x20);
		} else if (command >= I8042_WRITE_CTL_RAM &&
			   command <= I8042_WRITE_CTL_RAM_END) {
			data_port_state = STATE_WRITE_CMD_BYTE;
			controller_ram_address = command - 0x60;
		} else if (command == I8042_DISABLE_A20) {
			A20_status = 0;
		} else if (command == I8042_ENABLE_A20) {
			A20_status = 1;
		} else if (command >= I8042_PULSE_START &&
			   command <= I8042_PULSE_END) {
			/* Pulse Output Bits,
			 *   b0=0 to reset CPU, see I8042_SYSTEM_RESET above
			 *   b1=0 to disable A20 line
			 */
			A20_status = command & (1 << 1) ? 1 : 0;
		} else {
			CPRINTF("[%T KB unsupported cmd: 0x%02x]\n", command);
			reset_rate_and_delay();
			keyboard_clear_buffer();
			output[out_len++] = I8042_RET_NAK;
			data_port_state = STATE_NORMAL;
		}
		break;
	}

	return out_len;
}

static void i8042_handle_from_host(void)
{
	struct host_byte h;
	int ret_len;
	uint8_t output[MAX_SCAN_CODE_LEN];

	while (queue_remove_unit(&from_host, &h)) {
		if (h.type == HOST_COMMAND)
			ret_len = handle_keyboard_command(h.byte, output);
		else
			ret_len = handle_keyboard_data(h.byte, output);

		i8042_send_to_host(ret_len, output);
	}
}

/* U U D D L R L R b a */
static void keyboard_special(uint16_t k)
{
	static uint8_t s;
	static const uint16_t a[] = {0xe048, 0xe048, 0xe050, 0xe050, 0xe04b,
				     0xe04d, 0xe04b, 0xe04d, 0x0030, 0x001e};
#ifdef HAS_TASK_LIGHTBAR
	/* Lightbar demo mode: keyboard can fake the battery state */
	switch (k) {
	case 0xe048:				/* up */
		demo_battery_level(1);
		break;
	case 0xe050:				/* down */
		demo_battery_level(-1);
		break;
	case 0xe04b:				/* left */
		demo_is_charging(0);
		break;
	case 0xe04d:				/* right */
		demo_is_charging(1);
		break;
	case 0x0040:				/* dim */
		demo_brightness(-1);
		break;
	case 0x0041:				/* bright */
		demo_brightness(1);
		break;
	}
#endif

	if (k == a[s])
		s++;
	else if (k != 0xe048)
		s = 0;
	else if (s != 2)
		s = 1;

	if (s == ARRAY_SIZE(a)) {
		s = 0;
#ifdef HAS_TASK_LIGHTBAR
		lightbar_sequence(LIGHTBAR_KONAMI);
#endif
	}
}

void keyboard_protocol_task(void)
{
	int wait = -1;

	reset_rate_and_delay();

	while (1) {
		/* Wait for next host read/write */
		task_wait_event(wait);

		while (1) {
			timestamp_t t = get_time();
			uint8_t chr;

			/* Handle typematic */
			if (!typematic_len) {
				/* Typematic disabled; wait for enable */
				wait = -1;
			} else if (timestamp_expired(typematic_deadline, &t)) {
				/* Ready for next typematic keystroke */
				if (keystroke_enabled)
					i8042_send_to_host(typematic_len,
							   typematic_scan_code);
				typematic_deadline.val = t.val +
					typematic_inter_delay;
				wait = typematic_inter_delay;
			} else {
				/* Wait for remaining interval */
				wait = typematic_deadline.val - t.val;
			}

			/* Handle command/data write from host */
			i8042_handle_from_host();

			/* Check if we have data to send to host */
			if (queue_is_empty(&to_host))
				break;

			/* Host interface must have space */
			if (lpc_keyboard_has_char())
				break;

			/* Get a char from buffer. */
			kblog_put('k', to_host.head);
			queue_remove_unit(&to_host, &chr);
			kblog_put('K', chr);

			/* Write to host. */
			lpc_keyboard_put_char(chr, i8042_irq_enabled);
		}
	}
}

/**
 * Handle button changing state.
 *
 * @param button	Type of button that changed
 * @param is_pressed	Whether the button was pressed or released
 */
test_mockable void keyboard_update_button(enum keyboard_button_type button,
					  int is_pressed)
{
	/* TODO(crosbug.com/p/24956): Add typematic repeat support. */

	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	uint16_t make_code;
	uint32_t len;
	enum scancode_set_list code_set;

	/*
	 * Only send the scan code if main chipset is fully awake and
	 * keystrokes are enabled.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || !keystroke_enabled)
		return;

	code_set = acting_code_set(scancode_set);
	make_code = button_scancodes[code_set - SCANCODE_SET_1][button];
	scancode_bytes(make_code, is_pressed, code_set, scan_code, &len);
	ASSERT(len > 0);
	if (keystroke_enabled) {
		i8042_send_to_host(len, scan_code);
		task_wake(TASK_ID_KEYPROTO);
	}
}

/*****************************************************************************/
/* Console commands */

static int command_typematic(int argc, char **argv)
{
	int i;

	if (argc == 3) {
		typematic_first_delay = strtoi(argv[1], NULL, 0) * MSEC;
		typematic_inter_delay = strtoi(argv[2], NULL, 0) * MSEC;
	}

	ccprintf("From host:    0x%02x\n", typematic_value_from_host);
	ccprintf("First delay: %d ms\n", typematic_first_delay / 1000);
	ccprintf("Inter delay: %d ms\n", typematic_inter_delay / 1000);
	ccprintf("Now:         %.6ld\n", get_time().val);
	ccprintf("Deadline:    %.6ld\n", typematic_deadline.val);

	ccputs("Repeat scan code:");
	for (i = 0; i < typematic_len; ++i)
		ccprintf(" 0x%02x", typematic_scan_code[i]);
	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typematic, command_typematic,
			"[first] [inter]",
			"Get/set typematic delays",
			NULL);


static int command_codeset(int argc, char **argv)
{
	if (argc == 2) {
		int set = strtoi(argv[1], NULL, 0);
		switch (set) {
		case SCANCODE_SET_1:  /* fall-thru */
		case SCANCODE_SET_2:  /* fall-thru */
			scancode_set = set;
			break;
		default:
			return EC_ERROR_PARAM1;
		}
	}

	ccprintf("Set: %d\n", scancode_set);
	ccprintf("I8042_XLATE: %d\n", controller_ram[0] & I8042_XLATE ? 1 : 0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(codeset, command_codeset,
			"[set]",
			"Get/set keyboard codeset",
			NULL);


static int command_controller_ram(int argc, char **argv)
{
	int index;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	index = strtoi(argv[1], NULL, 0);
	if (index >= ARRAY_SIZE(controller_ram))
		return EC_ERROR_PARAM1;

	if (argc >= 3)
		update_ctl_ram(index, strtoi(argv[2], NULL, 0));

	ccprintf("%d = 0x%02x\n", index, controller_ram[index]);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ctrlram, command_controller_ram,
			"index [value]",
			"Get/set keyboard controller RAM",
			NULL);

static int command_keyboard_log(int argc, char **argv)
{
	int i;

	/* If no args, print log */
	if (argc == 1) {
		ccprintf("KBC log (len=%d):\n", kblog_len);
		for (i = 0; kblog_buf && i < kblog_len; ++i) {
			ccprintf("%c.%02x ",
				 kblog_buf[i].type, kblog_buf[i].byte);
			if ((i & 15) == 15) {
				ccputs("\n");
				cflush();
			}
		}
		ccputs("\n");
		return EC_SUCCESS;
	}

	/* Otherwise, enable/disable */
	if (!parse_bool(argv[1], &i))
		return EC_ERROR_PARAM1;

	if (i) {
		if (!kblog_buf) {
			int rv = shared_mem_acquire(
				sizeof(*kblog_buf) * MAX_KBLOG,
				(char **)&kblog_buf);
			if (rv != EC_SUCCESS)
				kblog_buf = NULL;
			kblog_len = 0;
			return rv;
		}
	} else {
		kblog_len = 0;
		if (kblog_buf)
			shared_mem_release(kblog_buf);
		kblog_buf = NULL;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kblog, command_keyboard_log,
			"[on | off]",
			"Print or toggle keyboard event log",
			NULL);


static int command_keyboard(int argc, char **argv)
{
	int ena;

	if (argc > 1) {
		if (!parse_bool(argv[1], &ena))
			return EC_ERROR_PARAM1;

		keyboard_enable(ena);
	}

	ccprintf("Enabled: %d\n", keyboard_enabled);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbd, command_keyboard,
			"[0 | 1]",
			"Print or toggle keyboard info",
			NULL);

/*****************************************************************************/
/* Hooks */

/**
 * Preserve the states of keyboard controller to keep the initialized states
 * between reboot_ec commands. Saving info include:
 *
 *   - code set
 *   - controller_ram[0]:
 *     - XLATE
 *     - KB/TP disabled
 *     - KB/TP IRQ enabled
 */
static void keyboard_preserve_state(void)
{
	struct kb_state state;

	state.codeset = scancode_set;
	state.ctlram = controller_ram[0];
	state.keystroke_enabled = keystroke_enabled;

	system_add_jump_tag(KB_SYSJUMP_TAG, KB_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, keyboard_preserve_state, HOOK_PRIO_DEFAULT);

/**
 * Restore the keyboard states after reboot_ec command. See above function.
 */
static void keyboard_restore_state(void)
{
	const struct kb_state *prev;
	int version, size;

	prev = (const struct kb_state *)system_get_jump_tag(KB_SYSJUMP_TAG,
							    &version, &size);
	if (prev && version == KB_HOOK_VERSION && size == sizeof(*prev)) {
		/* Coming back from a sysjump, so restore settings. */
		scancode_set = prev->codeset;
		update_ctl_ram(0, prev->ctlram);
		keystroke_enabled = prev->keystroke_enabled;
	}
}
DECLARE_HOOK(HOOK_INIT, keyboard_restore_state, HOOK_PRIO_DEFAULT);

/**
 * Handle power button changing state.
 */
static void keyboard_power_button(void)
{
	keyboard_update_button(KEYBOARD_BUTTON_POWER,
			       power_button_is_pressed());
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, keyboard_power_button,
	     HOOK_PRIO_DEFAULT);
