
/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * i2c to PS2 compat mouse emulation using hid-i2c to ps2 conversion
 * This is designed to function with a pixart hid-i2c touchpad
 * and there are a few settings that configure this touchpad
 * mouse mode based on fixed assumptions from the hid descriptor
 * specifically the mode switch command.
 */
#include "system.h"
#include "task.h"
#include "config.h"
#include "console.h"
#include "task.h"
#include "hooks.h"
#include "util.h"
#include "i2c.h"
#include "timer.h"
#include "keyboard_8042.h"
#include "ps2mouse.h"
#include "power.h"
#include "diagnostics.h"
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

static enum ps2_mouse_state mouse_state = PS2MSTATE_STREAM;
static enum ps2_mouse_state prev_mouse_state = PS2MSTATE_STREAM;
static uint8_t prev_command;
static uint8_t data_report_en = true;
static int32_t current_pos[4] = {0x08, 0, 0, 0};
static uint8_t button_state;
/*in 5 button mode deltaz is changed by +-1 for vertical scroll, and +-2 for horizontal scroll*/
static uint8_t five_button_mode;
/* State of magic Knock to enter 5 button mode */
static uint8_t five_button_flags;
static uint8_t mouse_scale;
static uint8_t resolution;
static uint8_t sample_rate = 100;
static uint8_t ec_mode_disabled;
static uint8_t detected_host_packet = true;
static uint8_t emumouse_task_id;
static uint8_t aux_data;
void send_data_byte(uint8_t data) {
	int timeout = 0;

		/* sometimes the host will get behind */
	while (aux_buffer_available() < 1 && timeout++ < AUX_BUFFER_FULL_RETRIES)
		usleep(10*MSEC);
	send_aux_data_to_host_interrupt(data);
}
void send_movement_packet(void)
{
	int i;
	int max = 3;
	int timeout = 0;

	if (five_button_mode)
		max = 4;
	/* sometimes the host will get behind */
	while (aux_buffer_available() < max && timeout++ < AUX_BUFFER_FULL_RETRIES &&
			(*task_get_event_bitmap(emumouse_task_id) & PS2MOUSE_EVT_AUX_DATA) == 0) {
		usleep(10*MSEC);
	}

	if (timeout == AUX_BUFFER_FULL_RETRIES ||
		(*task_get_event_bitmap(emumouse_task_id) & PS2MOUSE_EVT_AUX_DATA)) {
		CPRINTS("PS2M Dropping");
		/*drop mouse packet - host is too far behind */
		return;
	}

	for (i = 0; i < max; i++) {
		send_data_byte(current_pos[i]);
	}
}

void send_aux_data_to_device(uint8_t data)
{
	aux_data = data;
	task_set_event(emumouse_task_id, PS2MOUSE_EVT_AUX_DATA, 0);
}
void process_request(uint8_t data)
{
	uint8_t response;
	if (ec_mode_disabled) {
		return;
	}
	switch (mouse_state) {
	case PS2MSTATE_RESET:
		send_data_byte(PS2MOUSE_ACKNOWLEDGE);
		send_data_byte(PS2MOUSE_BAT_SUCCESS);
		send_data_byte(PS2MOUSE_ID_PS2);
		mouse_state = PS2MSTATE_STREAM;
		mouse_scale = 1;
		five_button_mode = 0;
		break;
	case PS2MSTATE_CONSUME_1_BYTE:
		mouse_state = prev_mouse_state;
		break;
	case PS2MSTATE_CONSUME_1_BYTE_ACK:
		if (prev_command == PS2MOUSE_SET_SAMPLE_RATE) {
			if (five_button_flags == 0 && data == 200) {
				five_button_flags = BIT(0);
			} else if (five_button_flags == BIT(0) && data == 200) {
				five_button_flags |= BIT(1);
			} else if (five_button_flags == (BIT(0) | BIT(1)) && data == 80) {
				five_button_flags |= BIT(2);
				CPRINTS("PS2M 5 Button Magic Knock!");
				five_button_mode = 1;
			}
			sample_rate = data;
		} else if (prev_command == PS2MOUSE_SET_RESOLUTION) {
			resolution = data;
		}
		mouse_state = prev_mouse_state;
		goto SENDACK;
	case PS2MSTATE_STREAM:
	case PS2MSTATE_REMOTE:
		prev_command = data;
		switch (data) {
		case PS2MOUSE_RESET:
			mouse_state = PS2MSTATE_STREAM;
			send_data_byte(PS2MOUSE_ACKNOWLEDGE);
			send_data_byte(PS2MOUSE_BAT_SUCCESS);
			send_data_byte(PS2MOUSE_ID_PS2);
			five_button_mode = 0;
			five_button_flags = 0;
			break;
		case PS2MOUSE_READ_DATA:
			send_data_byte(PS2MOUSE_ACKNOWLEDGE);
		case PS2MOUSE_RESEND:
			CPRINTS("PS2M Got resend");
			send_movement_packet();
			break;
		case PS2MOUSE_SET_REMOTE_MODE:
			mouse_state = PS2MSTATE_REMOTE;
			goto SENDACK;
		case PS2MOUSE_DISABLE_DATA_REPORT:
			data_report_en = false;
			goto SENDACK;
		case PS2MOUSE_SET_STREAM_MODE:
		case PS2MOUSE_ENABLE_DATA_REPORT:
			data_report_en = true;
			mouse_state = PS2MSTATE_STREAM;
			goto SENDACK;
		case PS2MOUSE_SET_SAMPLE_RATE:
			prev_mouse_state = mouse_state;
			mouse_state = PS2MSTATE_CONSUME_1_BYTE_ACK;
			goto SENDACK;
		case PS2MOUSE_GET_DEVICE_ID:
			send_data_byte(PS2MOUSE_ACKNOWLEDGE);
			if (five_button_mode) {
				send_data_byte(PS2MOUSE_ID_INTELLIMOUSE_5BTN);
			} else {
				send_data_byte(PS2MOUSE_ID_PS2);
			}
			break;
		case PS2MOUSE_SET_WRAP_MODE:
			mouse_state = PS2MSTATE_WRAP;
			goto SENDACK;
		case PS2MOUSE_STATUS_REQUEST:
			send_data_byte(PS2MOUSE_ACKNOWLEDGE);
			response = mouse_state == PS2MSTATE_REMOTE ? BIT(6) : 0;
			response |= data_report_en ? STATUS_DATA_ENABLED : 0;
			send_data_byte(response);
			send_data_byte(resolution);
			send_data_byte(sample_rate);
			break;
		case PS2MOUSE_SET_SCALE_2:
			mouse_scale = 2;
			goto SENDACK;
		case PS2MOUSE_SET_SCALE_1:
			mouse_scale = 1;
			goto SENDACK;
		case PS2MOUSE_SET_RESOLUTION:
			prev_mouse_state = mouse_state;
			mouse_state = PS2MSTATE_CONSUME_1_BYTE_ACK;
			goto SENDACK;
		case PS2MOUSE_SET_DEFAULTS:
			goto SENDACK;
		default:
				CPRINTS("PS2 unhandled data 0x%x", data);
			goto SENDACK;

				break;
		}
		break;
	case PS2MSTATE_WRAP:
		if (data == PS2MOUSE_RESET || data == PS2MOUSE_RESET_WRAP_MODE) {
			mouse_state = PS2MSTATE_RESET;
			goto SENDACK;
		} else
			send_data_byte(data);
		break;
	default:
		CPRINTS("PS2 Invalid state 0x%x", mouse_state);
	}
	return;

SENDACK:
	send_data_byte(PS2MOUSE_ACKNOWLEDGE);
}

/*this interrupt is used to monitor if the main SOC is directly communicating with the
 * touchpad outside of the EC. If we detect this condition - then we disable the ec 8042
 * mouse emulation mode
 * */
void touchpad_i2c_interrupt(enum gpio_signal signal)
{
	task_set_event(emumouse_task_id, PS2MOUSE_EVT_I2C_INTERRUPT, 0);
	if (power_get_state() == POWER_S0 || power_get_state() == POWER_S0ix)
		detected_host_packet = true;
}
/* sometimes we get spurious interrupts from the host? that causes the tp to
 * get disabled but the touchpad will attempt to get the host to process
 * data by toggling the touchpad interrupt continuously every 100ms or so,
 * so we watch for several of these to reenable the touchpad
 */
timestamp_t last_int_time;
int unprocessed_tp_int_count;
void touchpad_interrupt(enum gpio_signal signal)
{
	timestamp_t now = get_time();
	if (ec_mode_disabled) {
		return;
	}
	if (!detected_host_packet) {
		task_set_event(emumouse_task_id, PS2MOUSE_EVT_INTERRUPT, 0);
		unprocessed_tp_int_count = 0;
	} else {
		if (timestamp_expired(last_int_time, &now)) {
			if (unprocessed_tp_int_count++ > TOUCHPAD_I2C_RETRY_COUNT_TO_RENABLE) {
				detected_host_packet = false;
				unprocessed_tp_int_count = 0;
				task_set_event(emumouse_task_id, PS2MOUSE_EVT_REENABLE, 0);
			}
		} else {
			unprocessed_tp_int_count = 0;
		}
	}
	last_int_time.val = now.val + 80*MSEC;
}

static void ps2mouse_powerstate_change(void)
{
	task_set_event(emumouse_task_id, PS2MOUSE_EVT_POWERSTATE, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME,
		ps2mouse_powerstate_change,
		HOOK_PRIO_DEFAULT);

DECLARE_HOOK(HOOK_CHIPSET_SUSPEND,
		ps2mouse_powerstate_change,
		HOOK_PRIO_DEFAULT);

/*
 * CMD REGISTER
 *	0xnXYZ
 *		X = op code, eg SET_POWER, SET_REPORT, RESET, GET_REPORT
 *		Y = report type, 0=res, 1=input, 2=output, 3=feature
 *		Z = Report ID
 * setup does the following
 *    CMD     DATA   ID
 * W 0x0022 0x0233 0x0023 opcode 0x02 - GET_REPORT, report id  0x3 INPUT_MODE_TOUCH, report type 3 - feature
 *	LEN     REPORT
 * R 0x0004 0x0503
 *
 *
 *       CMD   DATA  ID
 *  W 0x0022 0x0234 0x0023 opcode 0x02 - GET_REPORT, report id , report type 3 feature
 *    LEN     REPORT
 *  R 0x0004 0x0004
 *
 *  The following issue set report opcodes
 *    CMDREG  DATA  DATAREG LEN    DATA
 *  W 0x0022 0x0336 0x0023 0x0004 0x0306 opcode 0x03 feature report set configuration register to mode touch
 *
 *  W 0x0022 0x0337 0x0023 0x0004 0x0307 opcode 0x03 feature report set switch mode
 *
 *  W 0x0022 0x0338 0x0023 0x0004 0x0008 opcode 0x03 feature report set latency mode
 *
 *  I think if we want to set mouse mode we would do:
 *		0x0022 0x0336 0x0023 0x0004 0x0006 which sets feature report id 6 to input mode mouse
 */

void set_ps2_mouse_emulation(bool disable)
{
	if (disable)
		task_set_event(emumouse_task_id, PS2MOUSE_EVT_HC_DISABLE, 0);
	else
		task_set_event(emumouse_task_id, PS2MOUSE_EVT_HC_ENABLE, 0);
}
void set_power(bool standby)
{
	int data = BIT(11) + standby ? 1 : 0;

	i2c_write_offset16(I2C_PORT_TOUCHPAD, TOUCHPAD_I2C_HID_EP | I2C_FLAG_ADDR16_LITTLE_ENDIAN, PCT3854_COMMAND, data, 1);
}

void setup_touchpad(void)
{
	int rv;
	/* These are touchpad firmware dependent
	 * They set the touchpad into the mouse device mode, instead of PTP mode
	 * And are based on the HID descriptor for our unique device */
	const static uint16_t cmd[4] = {0x0336, 0x0023, 0x0004, 0x0006};

	rv = i2c_write_offset16_block(I2C_PORT_TOUCHPAD,
									TOUCHPAD_I2C_HID_EP | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
									PCT3854_COMMAND,
									(void *)cmd, ARRAY_SIZE(cmd) * sizeof(uint16_t));
	if (rv == EC_SUCCESS) {
		CPRINTS("Touchpad detected!");
	}
}

static void retry_tp_read_evt_deferred(void)
{
	task_set_event(emumouse_task_id, PS2MOUSE_EVT_INTERRUPT, 0);
}
DECLARE_DEFERRED(retry_tp_read_evt_deferred);

static int inreport_retries;
void read_touchpad_in_report(void)
{
	int rv = EC_SUCCESS;
	int need_reset = 0;
	uint8_t data[128];
	int xfer_len = 0;
	int16_t x, y;
	uint8_t response_byte = 0x08;

	/* Make sure report id is set to an invalid value */
	data[2] = 0;

	if (power_get_state() == POWER_S5) {
		return;
	}

	/*dont trigger disable state during our own transactions*/
	gpio_disable_interrupt(GPIO_EC_I2C_3_SDA);
	/* need to disable SOC_TP_INT_L if we need to setup touchpad */
	gpio_disable_interrupt(GPIO_SOC_TP_INT_L);
	i2c_set_timeout(I2C_PORT_TOUCHPAD, 25*MSEC);
	i2c_lock(I2C_PORT_TOUCHPAD, 1);
	rv = i2c_xfer_unlocked(I2C_PORT_TOUCHPAD,
							TOUCHPAD_I2C_HID_EP | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
							NULL, 0, data, 2, I2C_XFER_START);
	if (rv != EC_SUCCESS)
		goto read_failed;
	xfer_len = (data[1]<<8) + data[0];
	if (xfer_len == 0) {
		/**
		 * touchpad has reset per i2c-hid-protocol 7.3
		 * We need to complete the read protocol to make sure i2c state machine
		 * is correct.
		 */
		CPRINTS("PS2M Touchpad need to reset");
		xfer_len = 6;
		need_reset = 1;
	}
	xfer_len = MIN(126, xfer_len-2);
	rv = i2c_xfer_unlocked(I2C_PORT_TOUCHPAD,
							TOUCHPAD_I2C_HID_EP | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
							NULL, 0, data+2, xfer_len, I2C_XFER_STOP);
	if (rv != EC_SUCCESS)
		goto read_failed;
read_failed:
	if (rv != EC_SUCCESS) {
		/* sometimes we get a read failed for unknown reason to try again in a while
		 * to recover
		 */
		inreport_retries++;
		if (inreport_retries > 10) {
			/* try again some other time later if the TP keeps interrupting us */
			detected_host_packet = true;
			inreport_retries = 0;
			usleep(10*MSEC);
			/*The EC SMB controller sometimes gets in a bad state, so try to recover */
			MCHP_I2C_CTRL(MCHP_I2C_CTRL4) = BIT(7) |
				BIT(6) |
				BIT(3) |
				BIT(0);
			CPRINTS("PS2M  %d Too many retries", rv);
		} else {
			hook_call_deferred(&retry_tp_read_evt_deferred_data, 25*MSEC);
		}

	} else {
		inreport_retries = 0;
	}
	i2c_lock(I2C_PORT_TOUCHPAD, 0);
	gpio_enable_interrupt(GPIO_EC_I2C_3_SDA);
	gpio_enable_interrupt(GPIO_SOC_TP_INT_L);
	 if (mouse_state == PS2MSTATE_RESET) {
		 return;
	 }
	/* Packet structure:
	 * first two bytes are length (LSB MSB) including length field?
	 * 3rd byte is report ID
	 * rest of the packet is the input report
	 */
	if (rv == EC_SUCCESS && data[2] == 0x02) {
		/*0x0800 02 04 feff 0000
		 *0x0800 02 04 fdff ffff */
		x = (int16_t)(data[4] + (data[5] << 8));
		y = -(int16_t)(data[6] + (data[7] << 8));
		x = MIN(255, MAX(x, -255));
		y = MIN(255, MAX(y, -255));
		/*button data*/
		response_byte |= data[3] & 0x03;
		if ((x & 0xFFFFFE00) != 0 && (x & 0xFFFFFE00) != 0xFFFFFE00)
			response_byte |= X_OVERFLOW;
		if (x & 0x100)
			response_byte |= X_SIGN;
		if ((y & 0xFFFFFE00) != 0 && (y & 0xFFFFFE00) != 0xFFFFFE00)
			response_byte |= Y_OVERFLOW;
		if (y & 0x100)
			response_byte |= Y_SIGN;
		/*CPRINTS("Mouse Packet: 0x%02x %d, %d", response_byte, x, y);*/
		current_pos[0] = response_byte;
		current_pos[1] = x;
		current_pos[2] = y;
		send_movement_packet();
	}

	if (need_reset) {
		CPRINTS("PS2M Unexpected Report ID %d reconfiguring", data[2]);
		setup_touchpad();
		need_reset = 0;
	}
}
/*
 * Looking at timing it takes the SOC about 2ms to grab a tp packet from start of
 * packet to interrupt cleared. however the very
 * first interrupt triggered by the touchpad may take much longer - I have measured around 25ms
 */
void mouse_interrupt_handler_task(void *p)
{
	int power_state = 0;
	int evt;
	int i;

	emumouse_task_id = task_get_current();
	while (1) {
		evt = task_wait_event(-1);
		/*host disabled this*/
		if (evt & PS2MOUSE_EVT_HC_DISABLE && ec_mode_disabled == false) {
			ec_mode_disabled = true;
			CPRINTS("PS2M HC Disable");
			gpio_disable_interrupt(GPIO_SOC_TP_INT_L);
			gpio_disable_interrupt(GPIO_EC_I2C_3_SDA);
		}
		if (evt & PS2MOUSE_EVT_HC_ENABLE && ec_mode_disabled == true) {
			CPRINTS("PS2M HC Enable");
			ec_mode_disabled = false;
			setup_touchpad();
			gpio_enable_interrupt(GPIO_SOC_TP_INT_L);
			gpio_enable_interrupt(GPIO_EC_I2C_3_SDA);
		}
		if (ec_mode_disabled == false) {
			if (evt & PS2MOUSE_EVT_AUX_DATA) {
				process_request(aux_data);
			} else if  (evt & PS2MOUSE_EVT_INTERRUPT) {
				/* at the expensive of a slight additional latency
				 * check to see if the soc has grabbed this out from under us
				 */
				for (i = 0; i < 4; i++) {
					usleep(MSEC);
					if (gpio_get_level(GPIO_SOC_TP_INT_L) == 1) {
						CPRINTS("PS2M Detected host packet during interrupt handling");
						detected_host_packet = true;
						break;
					}
				}
				if (detected_host_packet != true) {
					read_touchpad_in_report();
				}
			}

			if  (evt & PS2MOUSE_EVT_I2C_INTERRUPT) {
				if (detected_host_packet) {
					CPRINTS("PS2M detected host packet from i2c");
					gpio_disable_interrupt(GPIO_EC_I2C_3_SDA);
				}
			}
			if (evt & PS2MOUSE_EVT_POWERSTATE) {
				power_state = power_get_state();
				CPRINTS("PS2M Got S0 Event %d", power_state);
				if (!ec_mode_disabled &&
					(power_state == POWER_S3S0)) {
					CPRINTS("PS2M Configuring for ps2 emulation mode");
					/*tp takes about 80 ms to come up, wait a bit*/
					usleep(200*MSEC);
					setup_touchpad();

					gpio_enable_interrupt(GPIO_SOC_TP_INT_L);
					gpio_enable_interrupt(GPIO_EC_I2C_3_SDA);
				}
				if ((power_state == POWER_S3S0) && gpio_get_level(GPIO_SOC_TP_INT_L) == 0) {
					read_touchpad_in_report();
				}
				if (power_state == POWER_S0S3 || power_state == POWER_S5) {
					/* Power Down */
					gpio_disable_interrupt(GPIO_SOC_TP_INT_L);
					gpio_disable_interrupt(GPIO_EC_I2C_3_SDA);
				}
			}
			if (evt & PS2MOUSE_EVT_REENABLE) {
				CPRINTS("PS2M renabling");
				gpio_enable_interrupt(GPIO_SOC_TP_INT_L);
				gpio_enable_interrupt(GPIO_EC_I2C_3_SDA);
			}
		}
	}
}

static int command_emumouse(int argc, char **argv)
{
	int32_t btn_state, x, y, response_byte;
	char *e;

	if (argc == 2 && !strncmp(argv[1], "int", 3)) {
		CPRINTS("Triggering interrupt");
		task_set_event(emumouse_task_id, PS2MOUSE_EVT_INTERRUPT, 0);
	}
	if (argc == 2 && !strncmp(argv[1], "res", 3)) {
		CPRINTS("Resetting to auto");
		ec_mode_disabled = 0;
		data_report_en = 1;
		detected_host_packet = true;
		task_set_event(emumouse_task_id, PS2MOUSE_EVT_REENABLE, 0);
	}
	if (argc < 4) {
		CPRINTS("mouse state 0x%x data_report: 0x%x btn:0x%x", mouse_state, data_report_en, button_state);
		CPRINTS("X:0x%x Y:0x%x Z:0x%x ", current_pos[0], current_pos[1], current_pos[2]);
		CPRINTS("Emulation: %s ", ec_mode_disabled ? "Disabled" : "Auto");
		CPRINTS("HostCtl: %s ", detected_host_packet ? "Detected" : "Not Detected");
		CPRINTS("MouseState %d", mouse_state);
		return 0;
	}

	btn_state = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	x = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;
	y = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	response_byte = 0x08;
	response_byte |= btn_state & 0x03;
	if ((x & 0xFFFFFE00) != 0 && (x & 0xFFFFFE00) != 0xFFFFFE00)
		response_byte |= X_OVERFLOW;
	if (x & 0x100)
		response_byte |= X_SIGN;
	if ((y & 0xFFFFFE00) != 0 && (y & 0xFFFFFE00) != 0xFFFFFE00)
		response_byte |= Y_OVERFLOW;
	if (y & 0x100)
		response_byte |= Y_SIGN;
	current_pos[0] = response_byte;
	current_pos[1] = x;
	current_pos[2] = y;
	button_state = btn_state;
	if (mouse_state == PS2MSTATE_STREAM)
		send_movement_packet();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(emumouse, command_emumouse,
		"emumouse buttons posx posy",
		"Emulate ps2 mouse events on the 8042 aux channel");
