/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 8042 keyboard protocol
 */

#include "chipset.h"
#include "button.h"
#include "common.h"
#include "console.h"
#include "device_event.h"
#include "hooks.h"
#include "host_command.h"
#include "i8042_protocol.h"
#include "keyboard_8042_sharedlib.h"
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
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

#ifdef CONFIG_KEYBOARD_DEBUG
#define CPUTS5(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS5(format, args...) cprints(CC_KEYBOARD, format, ## args)
#else
#define CPUTS5(outstr)
#define CPRINTS5(format, args...)
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

/* Number of bytes host can get behind before we start generating extra IRQs
 * Switch from 3 to 5 to handle 5 byte PS2 Mouse packets without spamming
 */
#define KB_TO_HOST_RETRIES 5

/*
 * Mutex to control write access to the to-host buffer head.  Don't need to
 * mutex the tail because reads are only done in one place.
 */
static struct mutex to_host_mutex;


/* Queue command/data to the host */
enum {
	CHAN_KBD = 0,
	CHAN_AUX,
};
struct data_byte {
	uint8_t chan;
	uint8_t byte;
};

static struct queue const to_host = QUEUE_NULL(16, struct data_byte);

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
static struct queue const from_host = QUEUE_NULL(8, struct host_byte);

/* Queue aux data to the host from interrupt context. */
static struct queue const aux_to_host_queue = QUEUE_NULL(16, uint8_t);

static int i8042_keyboard_irq_enabled;
static int i8042_aux_irq_enabled;

/* i8042 global settings */
static int keyboard_enabled;	/* default the keyboard is disabled. */
static int aux_chan_enabled;	/* default the mouse is disabled. */
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
#define DEFAULT_TYPEMATIC_VALUE (BIT(5) | (3 << 0))
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

/*****************************************************************************/
/* Keyboard event log */

/* Log the traffic between EC and host -- for debug only */
#define MAX_KBLOG 512  /* Max events in keyboard log */

struct kblog_t {
	/*
	 * Type:
	 *
	 * s = byte enqueued to send to host
	 * a = aux byte enqueued to send to host
	 * t = to-host queue tail pointer before type='s' bytes enqueued
	 *
	 * d = data byte from host
	 * c = command byte from host
	 *
	 * k = to-host queue head pointer before byte dequeued
	 * K = byte actually sent to host via LPC
	 * A = byte actually sent to host via LPC as AUX
	 *
	 * x = to_host queue was cleared
	 *
	 * The to-host head and tail pointers are logged pre-wrapping to the
	 * queue size.  This means that they continually increment as units
	 * are dequeued and enqueued respectively.  Since only the bottom
	 * byte of the value is logged they will wrap every 256 units.
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
	queue_add_unit(&from_host, &h);
	task_wake(TASK_ID_KEYPROTO);
}

int keyboard_host_write_avaliable(void)
{
	return queue_space(&from_host);
}


/**
 * Enable keyboard IRQ generation.
 *
 * @param enable	Enable (!=0) or disable (0) IRQ generation.
 */
static void keyboard_enable_irq(int enable)
{
	CPRINTS5("KB IRQ %s", enable ? "enable" : "disable");

	i8042_keyboard_irq_enabled = enable;
	if (enable)
		lpc_keyboard_resume_irq();
}

/**
 * Enable mouse IRQ generation.
 *
 * @param enable	Enable (!=0) or disable (0) IRQ generation.
 */
static void aux_enable_irq(int enable)
{
	CPRINTS5("AUX IRQ %s", enable ? "enable" : "disable");

	i8042_aux_irq_enabled = enable;
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
 * @param chan		Channel to send data on
 */
static void i8042_send_to_host(int len, const uint8_t *bytes,
			       uint8_t chan)
{
	int i;
	struct data_byte data;

	/* Enqueue output data if there's space */
	mutex_lock(&to_host_mutex);

	for (i = 0; i < len; i++)
		kblog_put(chan == CHAN_AUX ? 'a' : 's', bytes[i]);

	if (queue_space(&to_host) >= len) {
		kblog_put('t', to_host.state->tail);
		for (i = 0; i < len; i++) {
			data.chan = chan;
			data.byte = bytes[i];
			queue_add_unit(&to_host, &data);
		}
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

static int is_supported_code_set(enum scancode_set_list set)
{
	return (set == SCANCODE_SET_1 || set == SCANCODE_SET_2);
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
		scan_code[(*len)++] = make_code >> 8;
		make_code &= 0xff;
	}

	switch (code_set) {
	case SCANCODE_SET_1:
		make_code = scancode_translate_set2_to_1(make_code);
		scan_code[(*len)++] = pressed ? make_code : (make_code | 0x80);
		break;

	case SCANCODE_SET_2:
		if (pressed) {
			scan_code[(*len)++] = make_code;
		} else {
			scan_code[(*len)++] = 0xf0;
			scan_code[(*len)++] = make_code;
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

	if (row >= KEYBOARD_ROWS || col >= keyboard_cols)
		return EC_ERROR_INVAL;

	make_code = get_scancode_set2(row, col);

#ifdef CONFIG_KEYBOARD_SCANCODE_CALLBACK
	{
		enum ec_error_list r = keyboard_scancode_callback(
				&make_code, pressed);
		if (r != EC_SUCCESS)
			return r;
	}
#endif

	code_set = acting_code_set(code_set);
	if (!is_supported_code_set(code_set)) {
		CPRINTS("KB scancode set %d unsupported", code_set);
		return EC_ERROR_UNIMPLEMENTED;
	}

	if (!make_code) {
		CPRINTS("KB scancode %d:%d missing", row, col);
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
	CPRINTS("KB Clear Buffer");
	mutex_lock(&to_host_mutex);
	kblog_put('x', queue_count(&to_host));
	queue_init(&to_host);
	mutex_unlock(&to_host_mutex);
	lpc_keyboard_clear_buffer();
}

static void keyboard_wakeup(void)
{
	/* send host event notice AP wake up  */
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
}

static void set_typematic_key(const uint8_t *scan_code, int32_t len)
{
	typematic_deadline.val = get_time().val + typematic_first_delay;
	memcpy(typematic_scan_code, scan_code, len);
	typematic_len = len;
}

void clear_typematic_key(void)
{
	typematic_len = 0;
}

void keyboard_state_changed(int row, int col, int is_pressed)
{
	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	int32_t len = 0;
	enum ec_error_list ret;

#ifdef CONFIG_KEYBOARD_DEBUG
	char mylabel = get_keycap_label(row, col);

	if (mylabel & KEYCAP_LONG_LABEL_BIT)
		CPRINTS("KB (%d,%d)=%d %s", row, col, is_pressed,
			get_keycap_long_label(mylabel & KEYCAP_LONG_LABEL_INDEX_BITMASK));
	else
		CPRINTS("KB (%d,%d)=%d %c", row, col, is_pressed, mylabel);
#endif

	ret = matrix_callback(row, col, is_pressed, scancode_set, scan_code,
			      &len);
	if (ret == EC_SUCCESS) {
		ASSERT(len > 0);
		if (keystroke_enabled)
			i8042_send_to_host(len, scan_code, CHAN_KBD);
	}

	if (is_pressed) {
		keyboard_wakeup();
		set_typematic_key(scan_code, len);
		task_wake(TASK_ID_KEYPROTO);
	} else {
		clear_typematic_key();
	}
}

static void keystroke_enable(int enable)
{
	if (!keystroke_enabled && enable)
		CPRINTS("KS enable");
	else if (keystroke_enabled && !enable)
		CPRINTS("KS disable");

	keystroke_enabled = enable;
}

static void keyboard_enable(int enable)
{
	if (!keyboard_enabled && enable)
		CPRINTS5("KB enable");
	else if (keyboard_enabled && !enable)
		CPRINTS5("KB disable");

	keyboard_enabled = enable;
}

static void aux_enable(int enable)
{
	if (!aux_chan_enabled && enable)
		CPRINTS5("AUX enabled");
	else if (aux_chan_enabled && !enable)
		CPRINTS5("AUX disabled");

	aux_chan_enabled = enable;
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
	CPRINTS5("KB set CTR_RAM(0x%02x)=0x%02x (old:0x%02x)",
		 addr, data, orig);

	if (addr == 0x00) {
		/* Keyboard enable/disable */

		/* Enable IRQ before enable keyboard (queue chars to host) */
		if (!(orig & I8042_ENIRQ1) && (data & I8042_ENIRQ1))
			keyboard_enable_irq(1);
		if (!(orig & I8042_ENIRQ12) && (data & I8042_ENIRQ12))
			aux_enable_irq(1);

		/* Handle the I8042_KBD_DIS bit */
		keyboard_enable(!(data & I8042_KBD_DIS));

		/* Handle the I8042_AUX_DIS bit */
		aux_enable(!(data & I8042_AUX_DIS));

		/*
		 * Disable IRQ after disable keyboard so that every char must
		 * have informed the host.
		 */
		if ((orig & I8042_ENIRQ1) && !(data & I8042_ENIRQ1))
			keyboard_enable_irq(0);
		if ((orig & I8042_ENIRQ12) && !(data & I8042_ENIRQ12))
			aux_enable_irq(0);
	}
}

/**
 * Handle the port 0x60 writes from host.
 *
 * Returns 1 if the event was handled.
 */
static int handle_mouse_data(uint8_t data, uint8_t *output, int *count)
{
	int out_len = 0;

	switch (data_port_state) {
	case STATE_ECHO_MOUSE:
		CPRINTS5("STATE_ECHO_MOUSE: 0x%02x", data);
		output[out_len++] = data;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SEND_TO_MOUSE:
		CPRINTS5("STATE_SEND_TO_MOUSE: 0x%02x", data);
		send_aux_data_to_device(data);
		data_port_state = STATE_NORMAL;
		break;

	default:  /* STATE_NORMAL */
		return 0;
	}

	ASSERT(out_len <= MAX_SCAN_CODE_LEN);

	*count = out_len;

	return 1;
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

	switch (data_port_state) {
	case STATE_SCANCODE:
		CPRINTS5("KB eaten by STATE_SCANCODE: 0x%02x", data);
		if (data == SCANCODE_GET_SET) {
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = scancode_set;
		} else {
			scancode_set = data;
			CPRINTS("KB scancode set to %d", scancode_set);
			output[out_len++] = I8042_RET_ACK;
		}
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SETLEDS:
		CPRINTS5("KB eaten by STATE_SETLEDS");
#ifdef CONFIG_CAPSLED_SUPPORT
		hx20_8042_led_control(data);
#endif
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_EX_SETLEDS_1:
		CPRINTS5("KB eaten by STATE_EX_SETLEDS_1");
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_EX_SETLEDS_2;
		break;

	case STATE_EX_SETLEDS_2:
		CPRINTS5("KB eaten by STATE_EX_SETLEDS_2");
		output[out_len++] = I8042_RET_ACK;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_WRITE_CMD_BYTE:
		CPRINTS5("KB eaten by STATE_WRITE_CMD_BYTE: 0x%02x",
			 data);
		update_ctl_ram(controller_ram_address, data);
		data_port_state = STATE_NORMAL;
		break;

	case STATE_WRITE_OUTPUT_PORT:
		CPRINTS5("KB eaten by STATE_WRITE_OUTPUT_PORT: 0x%02x",
			 data);
		A20_status = (data & BIT(1)) ? 1 : 0;
		data_port_state = STATE_NORMAL;
		break;

	case STATE_SETREP:
		CPRINTS5("KB eaten by STATE_SETREP: 0x%02x", data);
		set_typematic_delays(data);

		output[out_len++] = I8042_RET_ACK;
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
#ifdef CONFIG_8042_AUX
			aux_enable(1);
#endif
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET_DIS:
			output[out_len++] = I8042_RET_ACK;
			keystroke_enable(0);
#ifdef CONFIG_8042_AUX
			aux_enable(0);
#endif
			reset_rate_and_delay();
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET_DEF:
			output[out_len++] = I8042_RET_ACK;
			reset_rate_and_delay();
			keyboard_clear_buffer();
			break;

		case I8042_CMD_RESET:
			reset_rate_and_delay();
			keyboard_clear_buffer();
			output[out_len++] = I8042_RET_ACK;
			output[out_len++] = 0xaa; /* keyboard BAT test success */
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
			CPRINTS("KB Unsupported i8042 data 0x%02x",
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

	CPRINTS5("KB recv cmd: 0x%02x", command);
	kblog_put('c', command);

	switch (command) {
	case I8042_READ_CMD_BYTE:
		/*
		 * Ensure that the keyboard buffer is cleared before adding
		 * command byte to it. Since the host is asking for command
		 * byte, sending it buffered key press data can confuse the
		 * host and result in it taking incorrect action.
		 */
		keyboard_clear_buffer();
		output[out_len++] = read_ctl_ram(0);
		break;

	case I8042_WRITE_CMD_BYTE:
		data_port_state = STATE_WRITE_CMD_BYTE;
		controller_ram_address = command - 0x60;
		break;

	case I8042_DIS_KB:
		update_ctl_ram(0, read_ctl_ram(0) | I8042_KBD_DIS);
		reset_rate_and_delay();
		typematic_len = 0;  /* stop typematic */
		keyboard_clear_buffer();
		break;

	case I8042_ENA_KB:
		update_ctl_ram(0, read_ctl_ram(0) & ~I8042_KBD_DIS);
		keystroke_enable(1);
		keyboard_clear_buffer();
		break;

	case I8042_READ_OUTPUT_PORT:
		output[out_len++] =
			(lpc_aux_has_char() ? BIT(5) : 0) |
			(lpc_keyboard_has_char() ? BIT(4) : 0) |
			(A20_status ? BIT(1) : 0) |
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
		aux_enable(0);
		break;

	case I8042_ENA_MOUSE:
		update_ctl_ram(0, read_ctl_ram(0) & ~I8042_AUX_DIS);
		aux_enable(1);
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
		chipset_reset(CHIPSET_RESET_KB_SYSRESET);
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
			A20_status = command & BIT(1) ? 1 : 0;
		} else {
			CPRINTS("KB unsupported cmd: 0x%02x", command);
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
	uint8_t chan = CHAN_KBD;

	while (queue_remove_unit(&from_host, &h)) {
		if (h.type == HOST_COMMAND) {
			ret_len = handle_keyboard_command(h.byte, output);
		} else {
			CPRINTS5("KB recv data: 0x%02x", h.byte);
			kblog_put('d', h.byte);

			if (IS_ENABLED(CONFIG_8042_AUX) &&
			    handle_mouse_data(h.byte, output, &ret_len))
				chan = CHAN_AUX;
			else
				ret_len = handle_keyboard_data(h.byte, output);
		}

		i8042_send_to_host(ret_len, output, chan);
	}
}

void keyboard_protocol_task(void *u)
{
	int wait = -1;
	int retries = 0;

	reset_rate_and_delay();

	while (1) {
		/* Wait for next host read/write */
		task_wait_event(wait);

		while (1) {
			timestamp_t t = get_time();
			struct data_byte entry;
#ifdef CONFIG_KEYBOARD_DEBUG
			cflush();
#endif
			/* Handle typematic */
			if (!typematic_len) {
				/* Typematic disabled; wait for enable */
				wait = -1;
			} else if (timestamp_expired(typematic_deadline, &t)) {
				/* Ready for next typematic keystroke */
				if (keystroke_enabled)
					i8042_send_to_host(typematic_len,
							   typematic_scan_code,
							   CHAN_KBD);
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

			/* Handle data waiting for host */
			if (lpc_keyboard_has_char()) {
				/* If interrupts disabled, nothing we can do */
				if (!i8042_keyboard_irq_enabled &&
				    !i8042_aux_irq_enabled)
					break;

				/* Give the host a little longer to respond */
				if (++retries < KB_TO_HOST_RETRIES)
					break;

				/*
				 * We keep getting data, but the host keeps
				 * ignoring us.  Fine, we're done waiting.
				 * Hey, host, are you ever gonna get to this
				 * data?  Send it another interrupt in case it
				 * somehow missed the first one.
				 */
				CPRINTS("KB extra IRQ");
				lpc_keyboard_resume_irq();
				retries = 0;
				break;
			}

			/* Get a char from buffer. */
			kblog_put('n', to_host.state->head);
			queue_remove_unit(&to_host, &entry);

			/* Write to host. */
			if (entry.chan == CHAN_AUX &&
			    IS_ENABLED(CONFIG_8042_AUX)) {
				kblog_put('A', entry.byte);
				lpc_aux_put_char(entry.byte,
						 i8042_aux_irq_enabled);
			} else {
				kblog_put('K', entry.byte);
				lpc_keyboard_put_char(
					entry.byte, i8042_keyboard_irq_enabled);
			}
			retries = 0;
		}
	}
}

static void send_aux_data_to_host_deferred(void)
{
	uint8_t data;

	if (IS_ENABLED(CONFIG_DEVICE_EVENT) &&
		chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		device_set_single_event(EC_DEVICE_EVENT_TRACKPAD);

	while (!queue_is_empty(&aux_to_host_queue)) {
		queue_remove_unit(&aux_to_host_queue, &data);
		if (aux_chan_enabled && IS_ENABLED(CONFIG_8042_AUX))
			i8042_send_to_host(1, &data, CHAN_AUX);
		else
			CPRINTS("AUX Callback ignored");
	}
}
DECLARE_DEFERRED(send_aux_data_to_host_deferred);

/**
 * Send aux data to host from interrupt context.
 *
 * @param data	Aux response to send to host.
 */
void send_aux_data_to_host_interrupt(uint8_t data)
{
	queue_add_unit(&aux_to_host_queue, &data);
	hook_call_deferred(&send_aux_data_to_host_deferred_data, 0);
}


int aux_buffer_available(void)
{
	return queue_space(&aux_to_host_queue);
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
	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	uint32_t len;
	struct button_8042_t button_8042;
	enum scancode_set_list code_set;

	/*
	 * Only send the scan code if main chipset is fully awake and
	 * keystrokes are enabled.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || !keystroke_enabled)
		return;

	code_set = acting_code_set(scancode_set);
	if (!is_supported_code_set(code_set))
		return;

	button_8042 = buttons_8042[button];
	scancode_bytes(button_8042.scancode, is_pressed, code_set, scan_code,
		       &len);
	ASSERT(len > 0);

	if (button_8042.repeat) {
		if (is_pressed)
			set_typematic_key(scan_code, len);
		else
			clear_typematic_key();
	}

	if (keystroke_enabled) {
		i8042_send_to_host(len, scan_code, CHAN_KBD);
		task_wake(TASK_ID_KEYPROTO);
	}
}

#ifdef CONFIG_SIMULATE_KEYCODE
void simulate_keyboard(uint16_t scancode, int is_pressed)
{
	uint8_t scan_code[MAX_SCAN_CODE_LEN];
	uint32_t len;
	enum scancode_set_list code_set;

	/*
	 * Only send the scan code if main chipset is fully awake and
	 * keystrokes are enabled.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON) || !keystroke_enabled)
		return;

	code_set = acting_code_set(scancode_set);
	if (!is_supported_code_set(code_set))
		return;

	scancode_bytes(scancode, is_pressed, code_set, scan_code,
		       &len);
	ASSERT(len > 0);

	if (is_pressed)
		set_typematic_key(scan_code, len);
	else
		clear_typematic_key();

	if (keystroke_enabled) {
		i8042_send_to_host(len, scan_code, CHAN_KBD);
		task_wake(TASK_ID_KEYPROTO);
	}
}
#endif
/*****************************************************************************/
/* Console commands */
#ifdef CONFIG_CMD_KEYBOARD
static int command_typematic(int argc, char **argv)
{
	int i;

	if (argc == 3) {
		typematic_first_delay = strtoi(argv[1], NULL, 0) * MSEC;
		typematic_inter_delay = strtoi(argv[2], NULL, 0) * MSEC;
	}

	ccprintf("From host:   0x%02x\n", typematic_value_from_host);
	ccprintf("First delay: %3d ms\n", typematic_first_delay / 1000);
	ccprintf("Inter delay: %3d ms\n", typematic_inter_delay / 1000);
	ccprintf("Now:         %.6" PRId64 "\n", get_time().val);
	ccprintf("Deadline:    %.6" PRId64 "\n", typematic_deadline.val);

	ccputs("Repeat scan code: {");
	for (i = 0; i < typematic_len; ++i)
		ccprintf("0x%02x, ", typematic_scan_code[i]);
	ccputs("}\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typematic, command_typematic,
			"[first] [inter]",
			"Get/set typematic delays");


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
			"Get/set keyboard codeset");


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
			"Get/set keyboard controller RAM");

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
			int rv = SHARED_MEM_ACQUIRE_CHECK(
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
			"Print or toggle keyboard event log");


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
			"[on | off]",
			"Print or toggle keyboard info");


static int command_8042_internal(int argc, char **argv)
{
	int i;

	ccprintf("data_port_state=%d\n", data_port_state);
	ccprintf("i8042_keyboard_irq_enabled=%d\n", i8042_keyboard_irq_enabled);
	ccprintf("i8042_aux_irq_enabled=%d\n", i8042_aux_irq_enabled);
	ccprintf("keyboard_enabled=%d\n", keyboard_enabled);
	ccprintf("keystroke_enabled=%d\n", keystroke_enabled);
	ccprintf("aux_chan_enabled=%d\n", aux_chan_enabled);

	ccprintf("resend_command[]={");
	for (i = 0; i < resend_command_len; i++)
		ccprintf("0x%02x, ", resend_command[i]);
	ccprintf("}\n");

	ccprintf("controller_ram_address=0x%02x\n", controller_ram_address);
	ccprintf("A20_status=%d\n", A20_status);

	ccprintf("from_host[]={");
	for (i = 0; i < queue_count(&from_host); ++i) {
		struct host_byte entry;

		queue_peek_units(&from_host, &entry, i, 1);

		ccprintf("0x%02x, 0x%02x, ", entry.type, entry.byte);
	}
	ccprintf("}\n");

	ccprintf("to_host[]={");
	for (i = 0; i < queue_count(&to_host); ++i) {
		struct data_byte entry;

		queue_peek_units(&to_host, &entry, i, 1);

		ccprintf("0x%02x%s, ", entry.byte,
			 entry.chan == CHAN_AUX ? " aux" : "");
	}
	ccprintf("}\n");

	return EC_SUCCESS;
}


static int command_8042(int argc, char **argv)
{
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "internal"))
			return command_8042_internal(argc, argv);
		else if (!strcasecmp(argv[1], "typematic"))
			return command_typematic(argc - 1, argv + 1);
		else if (!strcasecmp(argv[1], "codeset"))
			return command_codeset(argc - 1, argv + 1);
		else if (!strcasecmp(argv[1], "ctrlram"))
			return command_controller_ram(argc - 1, argv + 1);
		else if (!strcasecmp(argv[1], "kblog"))
			return command_keyboard_log(argc - 1, argv + 1);
		else if (!strcasecmp(argv[1], "kbd"))
			return command_keyboard(argc - 1, argv + 1);
		else
			return EC_ERROR_PARAM1;
	} else {
		char *ctlram_argv[] = {"ctrlram", "0"};

		ccprintf("\n- Typematic:\n");
		command_typematic(argc, argv);
		ccprintf("\n- Codeset:\n");
		command_codeset(argc, argv);
		ccprintf("\n- Control RAM:\n");
		command_controller_ram(
			sizeof(ctlram_argv) / sizeof(ctlram_argv[0]),
			ctlram_argv);
		ccprintf("\n- Keyboard log:\n");
		command_keyboard_log(argc, argv);
		ccprintf("\n- Keyboard:\n");
		command_keyboard(argc, argv);
		ccprintf("\n- Internal:\n");
		command_8042_internal(argc, argv);
		ccprintf("\n");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(8042, command_8042,
			"[internal | typematic | codeset | ctrlram |"
			" kblog | kbd]",
			"Print 8042 state in one place");
#endif


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
#ifndef CONFIG_IGNORED_BTN_SCANCODE
static void keyboard_power_button(void)
{
	keyboard_update_button(KEYBOARD_BUTTON_POWER,
			       power_button_is_pressed());
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, keyboard_power_button,
	     HOOK_PRIO_DEFAULT);
#endif
