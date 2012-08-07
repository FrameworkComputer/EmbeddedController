/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chrome OS EC keyboard common code.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "keyboard.h"
#include "i8042.h"
#include "hooks.h"
#include "host_command.h"
#include "lightbar.h"
#include "lpc.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "x86_power.h"

#define KEYBOARD_DEBUG 1

/* Console output macros */
#if KEYBOARD_DEBUG >= 1
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)
#else
#define CPUTS(outstr)
#define CPRINTF(format, args...)
#endif

#if KEYBOARD_DEBUG >= 5
#define CPUTS5(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTF5(format, args...) cprintf(CC_KEYBOARD, format, ## args)
#else
#define CPUTS5(outstr)
#define CPRINTF5(format, args...)
#endif

enum scancode_set_list {
	SCANCODE_GET_SET = 0,
	SCANCODE_SET_1,
	SCANCODE_SET_2,
	SCANCODE_SET_3,
	SCANCODE_MAX = SCANCODE_SET_3,
};


/*
 * i8042 global settings.
 */
static int keyboard_enabled = 0;  /* default the keyboard is disabled. */
static uint8_t resend_command[MAX_SCAN_CODE_LEN];
static uint8_t resend_command_len = 0;
static uint8_t controller_ram_address;
static uint8_t controller_ram[0x20] = {
	/* the so called "command byte" */
	I8042_XLATE | I8042_AUX_DIS | I8042_KBD_DIS,
	/* 0x01 - 0x1f are controller RAM */
};
static int power_button_pressed = 0;
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
#define DEFAULT_TYPEMATIC_VALUE ((1 << 5) || (1 << 3) || (3 << 0))
#define DEFAULT_FIRST_DELAY 500
#define DEFAULT_INTER_DELAY 91
#define TYPEMATIC_DELAY_UNIT 1000  /* 1ms = 1000us */
static uint8_t typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
static int refill_first_delay = DEFAULT_FIRST_DELAY;  /* unit: ms */
static int refill_inter_delay = DEFAULT_INTER_DELAY;  /* unit: ms */
static int typematic_delay;                           /* unit: us */
static int typematic_len = 0;  /* length of typematic_scan_code */
static uint8_t typematic_scan_code[MAX_SCAN_CODE_LEN];


#define KB_SYSJUMP_TAG 0x4b42  /* "KB" */
#define KB_HOOK_VERSION 1
/* the previous keyboard state before reboot_ec. */
struct kb_state {
	uint8_t codeset;
	uint8_t ctlram;
	uint8_t pad[2];  /* Pad to 4 bytes for system_add_jump_tag(). */
};


/* The standard Chrome OS keyboard matrix table. */
#define CROS_ROW_NUM 8  /* TODO: +1 for power button. */
#define CROS_COL_NUM 13
static const uint16_t scancode_set1[CROS_ROW_NUM][CROS_COL_NUM] = {
	{0x0000, 0xe05b, 0x003b, 0x0030, 0x0044, 0x0073, 0x0031, 0x0000, 0x000d,
	 0x0000, 0xe038, 0x0000, 0x0000},
	{0x0000, 0x0001, 0x003e, 0x0022, 0x0041, 0x0000, 0x0023, 0x0000, 0x0028,
	 0x0043, 0x0000, 0x000e, 0x0078},
	{0x001d, 0x000f, 0x003d, 0x0014, 0x0040, 0x001b, 0x0015, 0x0056, 0x001a,
	 0x0042, 0x0073, 0x0000, 0x0000},
	{0x0000, 0x0029, 0x003c, 0x0006, 0x003f, 0x0000, 0x0007, 0x0000, 0x000c,
	 0x0000, 0x0000, 0x002b, 0x0079},
	{0xe01d, 0x001e, 0x0020, 0x0021, 0x001f, 0x0025, 0x0024, 0x0000, 0x0027,
	 0x0026, 0x002b, 0x001c, 0x0000},
	{0x0000, 0x002c, 0x002e, 0x002f, 0x002d, 0x0033, 0x0032, 0x002a, 0x0035,
	 0x0034, 0x0000, 0x0039, 0x0000},
	{0x0000, 0x0002, 0x0004, 0x0005, 0x0003, 0x0009, 0x0008, 0x0000, 0x000b,
	 0x000a, 0x0038, 0xe050, 0xe04d},
	{0x0000, 0x0010, 0x0012, 0x0013, 0x0011, 0x0017, 0x0016, 0x0036, 0x0019,
	 0x0018, 0x0000, 0xe048, 0xe04b},
};

static const uint16_t scancode_set2[CROS_ROW_NUM][CROS_COL_NUM] = {
	{0x0000, 0xe01f, 0x0005, 0x0032, 0x0009, 0x0051, 0x0031, 0x0000, 0x0055,
	 0x0000, 0xe011, 0x0000, 0x0000},
	{0x0000, 0x0076, 0x000c, 0x0034, 0x0083, 0x0000, 0x0033, 0x0000, 0x0052,
	 0x0001, 0x0000, 0x0066, 0x0067},
	{0x0014, 0x000d, 0x0004, 0x002c, 0x000b, 0x005b, 0x0035, 0x0061, 0x0054,
	 0x000a, 0x0051, 0x0000, 0x0000},
	{0x0000, 0x000e, 0x0006, 0x002e, 0x0003, 0x0000, 0x0036, 0x0000, 0x004e,
	 0x0000, 0x0000, 0x005d, 0x0064},
	{0xe014, 0x001c, 0x0023, 0x002b, 0x001b, 0x0042, 0x003b, 0x0000, 0x004c,
	 0x004b, 0x005d, 0x005a, 0x0000},
	{0x0000, 0x001a, 0x0021, 0x002a, 0x0022, 0x0041, 0x003a, 0x0012, 0x004a,
	 0x0049, 0x0000, 0x0029, 0x0000},
	{0x0000, 0x0016, 0x0026, 0x0025, 0x001e, 0x003e, 0x003d, 0x0000, 0x0045,
	 0x0046, 0x0011, 0xe072, 0xe074},
	{0x0000, 0x0015, 0x0024, 0x002d, 0x001d, 0x0043, 0x003c, 0x0059, 0x004d,
	 0x0044, 0x0000, 0xe075, 0xe06b},
};


/* Recording which key is being simulated pressed. */
static uint8_t simulated_key[CROS_COL_NUM];


/* Log the traffic between EC and host -- for debug only */
#define MAX_KBLOG 512  /* Max events in keyboard log */
struct kblog_t {
	uint8_t type;
	uint8_t byte;
};
static struct kblog_t *kblog;  /* Log buffer, or NULL if not logging */
static int kblog_len;          /* Current log length */


/* Change to set 1 if the I8042_XLATE flag is set. */
static enum scancode_set_list acting_code_set(enum scancode_set_list set)
{
	if (controller_ram[0] & I8042_XLATE) {
		/* If the keyboard translation is enabled, then always
		 * generates set 1. */
		return SCANCODE_SET_1;
	}
	return set;
}


static enum ec_error_list matrix_callback(int8_t row, int8_t col,
					  int8_t pressed,
					  enum scancode_set_list code_set,
					  uint8_t *scan_code, int32_t* len)
{
	uint16_t make_code;

	ASSERT(scan_code);
	ASSERT(len);

	if (row > CROS_ROW_NUM || col > CROS_COL_NUM)
		return EC_ERROR_INVAL;

	if (pressed)
		keyboard_special(scancode_set1[row][col]);

	*len = 0;

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
		/* insert the break byte, move back the last byte and insert a
		 * 0xf0 byte before that. */
		if (!pressed) {
			ASSERT(*len >= 1);
			scan_code[*len] = scan_code[*len - 1];
			scan_code[*len - 1] = 0xF0;
			*len += 1;
		}
		break;
	default:
		break;
	}

	return EC_SUCCESS;
}


static void reset_rate_and_delay(void)
{
	typematic_value_from_host = DEFAULT_TYPEMATIC_VALUE;
	refill_first_delay = DEFAULT_FIRST_DELAY;
	refill_inter_delay = DEFAULT_INTER_DELAY;
}


void keyboard_clear_underlying_buffer(void)
{
	i8042_flush_buffer();
}


/*
 * TODO: Move this implementation to platform-dependent files.
 *       We don't do it now because not every board implement x86_power.c
 *         bds: no CONFIG_LPC and no CONFIG_TASK_X86POWER
 *         daisy(variants): no CONFIG_LPC and no CONFIG_TASK_X86POWER
 *       crosbug.com/p/8523
 */
static void keyboard_wakeup(void)
{
	host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
}


void keyboard_state_changed(int row, int col, int is_pressed)
{
	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	int32_t len;
	enum ec_error_list ret;

	CPRINTF5("[%T KB %s(): row=%d col=%d is_pressed=%d]\n",
		 __func__, row, col, is_pressed);

	ret = matrix_callback(row, col, is_pressed, scancode_set, scan_code,
			      &len);
	if (ret == EC_SUCCESS) {
		ASSERT(len > 0);
		if (keyboard_enabled)
			i8042_send_to_host(len, scan_code);
	}

	if (is_pressed) {
		keyboard_wakeup();

		typematic_delay = refill_first_delay * 1000;
		memcpy(typematic_scan_code, scan_code, len);
		typematic_len = len;
		task_wake(TASK_ID_TYPEMATIC);
	} else {
		typematic_len = 0;
	}
}


static void keyboard_enable(int enable)
{
	if (!keyboard_enabled && enable) {
		CPRINTF("[%T KB enable]\n");
	} else if (keyboard_enabled && !enable) {
		CPRINTF("[%T KB disable]\n");
		reset_rate_and_delay();
		typematic_len = 0;  /* stop typematic */
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


/* Manipulate the controller_ram[]. Some bits change may trigger internal
 * state change. */
static void update_ctl_ram(uint8_t addr, uint8_t data)
{
	uint8_t orig;

	if (addr >= ARRAY_SIZE(controller_ram))
		return;

	orig = controller_ram[addr];
	controller_ram[addr] = data;
	CPRINTF5("[%T KB set CTR_RAM(0x%02x)=0x%02x (old:0x%02x)]\n",
		 addr, data, orig);

	if (addr == 0x00) {  /* the controller RAM */
		/* Enable IRQ before enable keyboard (queue chars to host) */
		if (!(orig & I8042_ENIRQ1) && (data & I8042_ENIRQ1))
			i8042_enable_keyboard_irq();

		/* Handle the I8042_KBD_DIS bit */
		keyboard_enable(!(data & I8042_KBD_DIS));

		/* Disable IRQ after disable keyboard so that every char
		 * must have informed the host. */
		if ((orig & I8042_ENIRQ1) && !(data & I8042_ENIRQ1))
			i8042_disable_keyboard_irq();
	}
}


static enum {
	STATE_NORMAL = 0,
	STATE_SCANCODE,
	STATE_SETLEDS,
	STATE_EX_SETLEDS_1,  /* expect 2-byte parameter coming */
	STATE_EX_SETLEDS_2,
	STATE_WRITE_CMD_BYTE,
	STATE_ECHO_MOUSE,
	STATE_SETREP,
	STATE_SEND_TO_MOUSE,
} data_port_state = STATE_NORMAL;


int handle_keyboard_data(uint8_t data, uint8_t *output)
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

	case STATE_ECHO_MOUSE:
		CPRINTF5("[%T KB eaten by STATE_ECHO_MOUSE: 0x%02x]\n", data);
		output[out_len++] = data;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SETREP:
		CPRINTF5("[%T KB eaten by STATE_SETREP: 0x%02x]\n", data);
		typematic_value_from_host = data;
		refill_first_delay =
			(((typematic_value_from_host & 0x60) >> 5) + 1) * 250;
		refill_inter_delay = 1000 *  /* ms */
			(1 << ((typematic_value_from_host & 0x18) >> 3)) *
			((typematic_value_from_host & 0x7) + 8) /
			240;

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
			/* We use screen indicator. Do nothing in keyboard
			 * controller. */
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
			keyboard_enable(1);
			keyboard_clear_underlying_buffer();
			break;

		case I8042_CMD_RESET_DIS:
			output[out_len++] = I8042_RET_ACK;
			keyboard_enable(0);
			reset_rate_and_delay();
			keyboard_clear_underlying_buffer();
			break;

		case I8042_CMD_RESET_DEF:
			output[out_len++] = I8042_RET_ACK;
			reset_rate_and_delay();
			keyboard_clear_underlying_buffer();
			break;

		case I8042_CMD_RESET_BAT:
			reset_rate_and_delay();
			keyboard_clear_underlying_buffer();
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = I8042_RET_BAT;
			output[out_len++] = I8042_RET_BAT;
			break;

		case I8042_CMD_RESEND:
			save_for_resend = 0;
			for (i = 0; i < resend_command_len; ++i)
				output[out_len++] = resend_command[i];
			break;

		/* u-boot hack */
		case 0x60:  /* see CONFIG_USE_CPCIDVI in */
		case 0x45:  /* third_party/u-boot/files/drivers/input/i8042.c */
			/* just ignore, don't reply anything. */
			break;

		case I8042_CMD_SETALL_MB:  /* fall-thru below */
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


int handle_keyboard_command(uint8_t command, uint8_t *output)
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

#ifdef CONFIG_TASK_X86POWER
	case I8042_SYSTEM_RESET:
		x86_power_reset(0);
		break;
#endif

	default:
		if (command >= I8042_READ_CTL_RAM &&
		    command <= I8042_READ_CTL_RAM_END) {
			output[out_len++] = read_ctl_ram(command - 0x20);
		} else if (command >= I8042_WRITE_CTL_RAM &&
			   command <= I8042_WRITE_CTL_RAM_END) {
			data_port_state = STATE_WRITE_CMD_BYTE;
			controller_ram_address = command - 0x60;
		} else if (command >= I8042_PULSE_START &&
			   command <= I8042_PULSE_END) {
			/* Pulse Output Bit. Not implemented. Ignore it. */
		} else {
			CPRINTF("[%T KB unsupported cmd: 0x%02x]\n", command);
			reset_rate_and_delay();
			keyboard_clear_underlying_buffer();
			output[out_len++] = I8042_RET_NAK;
			data_port_state = STATE_NORMAL;
		}
		break;
	}

	return out_len;
}


/* U U D D L R L R b a */
static void keyboard_special(uint16_t k)
{
	static uint8_t s = 0;
	static const uint16_t a[] = {0xe048, 0xe048, 0xe050, 0xe050, 0xe04b,
				     0xe04d, 0xe04b, 0xe04d, 0x0030, 0x001e};
	if (k == a[s])
		s++;
	else if (k != 0xe048)
		s = 0;
	else if (s != 2)
		s = 1;

	if (s == ARRAY_SIZE(a)) {
		s = 0;
#ifdef CONFIG_TASK_LIGHTBAR
		lightbar_sequence(LIGHTBAR_KONAMI);
#endif
	}
}


void keyboard_set_power_button(int pressed)
{
	enum scancode_set_list code_set;
	enum ec_error_list ret;
	uint8_t code[2][2][3] = {
		{  /* set 1 */
			{0xe0, 0xde},        /* break */
			{0xe0, 0x5e},        /* make */
		}, {  /* set 2 */
			{0xe0, 0xf0, 0x37},  /* break */
			{0xe0, 0x37},        /* make */
		}
	};

	power_button_pressed = pressed;

	/* Only send the scan code if main chipset is fully awake */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	code_set = acting_code_set(scancode_set);
	if (keyboard_enabled) {
		ret = i8042_send_to_host(
			 (code_set == SCANCODE_SET_2 && !pressed) ? 3 : 2,
			 code[code_set - SCANCODE_SET_1][pressed]);
		ASSERT(ret == EC_SUCCESS);
	}
}


void kblog_put(char type, uint8_t byte)
{
	if (kblog && kblog_len < MAX_KBLOG) {
		kblog[kblog_len].type = type;
		kblog[kblog_len].byte = byte;
		kblog_len++;
	}
}


void keyboard_typematic_task(void)
{
	while (1) {
		task_wait_event(-1);

		while (typematic_len) {
			usleep(TYPEMATIC_DELAY_UNIT);
			typematic_delay -= TYPEMATIC_DELAY_UNIT;

			if (typematic_delay <= 0) {
				/* re-send to host */
				if (keyboard_enabled)
					i8042_send_to_host(typematic_len,
							   typematic_scan_code);
				typematic_delay = refill_inter_delay * 1000;
			}
		}
	}
}

/*****************************************************************************/
/* Console commands */

static int command_typematic(int argc, char **argv)
{
	int i;

	if (argc == 3) {
		refill_first_delay = strtoi(argv[1], NULL, 0);
		refill_inter_delay = strtoi(argv[2], NULL, 0);
	}

	ccprintf("From host:    0x%02x\n", typematic_value_from_host);
	ccprintf("First delay: %d ms\n", refill_first_delay);
	ccprintf("Inter delay: %d ms\n", refill_inter_delay);
	ccprintf("Current:     %d ms\n", typematic_delay / 1000);

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


static int command_keyboard_press(int argc, char **argv)
{
	if (argc == 1) {
		int i, j;

		ccputs("Simulated key:\n");
		for (i = 0; i < CROS_COL_NUM; ++i) {
			if (simulated_key[i] == 0)
				continue;
			for (j = 0; j < CROS_ROW_NUM; ++j)
				if (simulated_key[i] & (1 << j))
					ccprintf("\t%d %d\n", i, j);
		}

	} else if (argc == 4) {
		int r, c, p;
		char *e;

		c = strtoi(argv[1], &e, 0);
		if (*e || c < 0 || c >= CROS_COL_NUM)
			return EC_ERROR_PARAM1;

		r = strtoi(argv[2], &e, 0);
		if (*e || r < 0 || r >= CROS_ROW_NUM)
			return EC_ERROR_PARAM2;

		p = strtoi(argv[3], &e, 0);
		if (*e || p < 0 || p > 1)
			return EC_ERROR_PARAM3;

		if ((simulated_key[c] & (1 << r)) == (p << r))
			return EC_SUCCESS;

		simulated_key[c] = (simulated_key[c] & ~(1 << r)) | (p << r);

		keyboard_state_changed(r, c, p);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbpress, command_keyboard_press,
			"[col] [row] [0 | 1]",
			"Simulate keypress",
			NULL);


static int command_keyboard_log(int argc, char **argv)
{
	int i;

	if (argc == 1) {
		ccprintf("KBC log (len=%d):\n", kblog_len);
		for (i = 0; kblog && i < kblog_len; ++i) {
			ccprintf("%c.%02x ", kblog[i].type, kblog[i].byte);
			if ((i & 15) == 15) {
				ccputs("\n");
				cflush();
			}
		}
		ccputs("\n");
	} else if (argc == 2 && !strcasecmp("on", argv[1])) {
		if (!kblog) {
			int rv = shared_mem_acquire(sizeof(*kblog) * MAX_KBLOG,
						    1, (char **)&kblog);
			if (rv != EC_SUCCESS)
				kblog = NULL;
			kblog_len = 0;
			return rv;
		}
	} else if (argc == 2 && !strcasecmp("off", argv[1])) {
		kblog_len = 0;
		if (kblog)
			shared_mem_release(kblog);
		kblog = NULL;
	} else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kblog, command_keyboard_log,
			"[on | off]",
			"Print or toggle keyboard event log",
			NULL);


static int command_keyboard(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp(argv[1], "enable"))
			keyboard_enable(1);
		else if (!strcasecmp(argv[1], "disable"))
			keyboard_enable(0);
		else
			return EC_ERROR_PARAM1;
	}

	ccprintf("Enabled: %d\n", keyboard_enabled);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbd, command_keyboard,
			"[enable | disable]",
			"Print or toggle keyboard info",
			NULL);

/*****************************************************************************/
/* Host commands */

static int mkbp_command_simulate_key(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_simulate_key *p = args->params;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->col >= ARRAY_SIZE(simulated_key))
		return EC_RES_INVALID_PARAM;

	simulated_key[p->col] = (simulated_key[p->col] & ~(1 << p->row)) |
				(p->pressed << p->row);

	keyboard_state_changed(p->row, p->col, p->pressed);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_SIMULATE_KEY,
		     mkbp_command_simulate_key,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Hooks */

/* Preserves the states of keyboard controller to keep the initialized states
 * between reboot_ec commands. Saving info include:
 *
 *   - code set
 *   - controller_ram[0]:
 *     - XLATE
 *     - KB/TP disabled
 *     - KB/TP IRQ enabled
 */
static int keyboard_preserve_state(void)
{
	struct kb_state state;

	state.codeset = scancode_set;
	state.ctlram = controller_ram[0];

	system_add_jump_tag(KB_SYSJUMP_TAG, KB_HOOK_VERSION,
	                    sizeof(state), &state);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_SYSJUMP, keyboard_preserve_state, HOOK_PRIO_DEFAULT);


/* Restores the keyboard states after reboot_ec command. See above function. */
static int keyboard_restore_state(void)
{
	const struct kb_state *prev;
	int version, size;

	prev = (const struct kb_state *)system_get_jump_tag(KB_SYSJUMP_TAG,
	                                                    &version, &size);
	if (prev && version == KB_HOOK_VERSION && size == sizeof(*prev)) {
		/* Coming back from a sysjump, so restore settings. */
		scancode_set = prev->codeset;
		update_ctl_ram(0, prev->ctlram);
	}

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, keyboard_restore_state, HOOK_PRIO_DEFAULT);
