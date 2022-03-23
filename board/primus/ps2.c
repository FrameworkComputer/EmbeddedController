/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stddef.h>

#include "cbi_ssfc.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "ps2.h"
#include "ps2_chip.h"
#include "time.h"
#include "registers.h"

#define	PS2_TRANSMIT_DELAY_MS	10

void send_aux_data_to_device(uint8_t data)
{
	ps2_transmit_byte(PRIMUS_PS2_CH, data);
}

static void board_init(void)
{
	ps2_enable_channel(PRIMUS_PS2_CH, 1, send_aux_data_to_host_interrupt);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*
 * Goodix touchpad AVDD need to pull low to 0V when poweroff.
 * Setting PS2 module in GPIO.inc will let AVDD have 0.9V offset.
 * So we need to enable PS2 module later than PLTRST# to avoid the 0.9V
 * offset.
 */
static void enable_ps2(void)
{
	gpio_set_alternate_function(GPIO_PORT_6,
		BIT(2) | BIT(3), GPIO_ALT_FUNC_DEFAULT);
}
DECLARE_DEFERRED(enable_ps2);

static void disable_ps2(void)
{
	gpio_set_flags(GPIO_EC_PS2_SCL_TPAD, GPIO_ODR_LOW);
	gpio_set_flags(GPIO_EC_PS2_SDA_TPAD, GPIO_ODR_LOW);
	gpio_set_alternate_function(GPIO_PORT_6,
		BIT(2) | BIT(3), GPIO_ALT_FUNC_NONE);
	/* make sure PLTRST# goes high and re-enable ps2.*/
	hook_call_deferred(&enable_ps2_data, 2 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, disable_ps2, HOOK_PRIO_DEFAULT);

static void ps2_transmit(uint8_t cmd)
{
	ps2_transmit_byte(PRIMUS_PS2_CH, cmd);
	msleep(PS2_TRANSMIT_DELAY_MS);
}

static void send_command_to_trackpoint(uint8_t command1, uint8_t command2)
{
	/*
	 * Send command to trackpoint and wait,
	 * this will make sure EC get ACK from PS2 device
	 * and send the next command.
	 */

	ps2_transmit(TP_COMMAND);
	ps2_transmit(TP_TOGGLE);
	ps2_transmit(command1);
	ps2_transmit(command2);
}

int get_trackpoint_id(void)
{
	if (get_cbi_ssfc_trackpoint() == SSFC_SENSOR_TRACKPOINT_ELAN)
		return TP_VARIANT_ELAN;
	else
		return TP_VARIANT_SYNAPTICS;
}

/* Called on AP S0 -> S3 transition */
static void ps2_suspend(void)
{
	int trackpoint_id;
	/*
	 * When EC send PS2 command to PS2 device,
	 * PS2 device will return ACK(0xFA).
	 * EC will send it to host and cause host wake from suspend.
	 * So disable EC send data to host to avoid it.
	 */
	ps2_enable_channel(PRIMUS_PS2_CH, 1, NULL);
	trackpoint_id = get_trackpoint_id();
	/*
	 * Send suspend mode to trackpoint
	 * Those commands was provide by Elan and Synaptics
	 */
	if (trackpoint_id == TP_VARIANT_ELAN)
		send_command_to_trackpoint(TP_TOGGLE_BURST,
			TP_TOGGLE_ELAN_SLEEP);
	else if (trackpoint_id == TP_VARIANT_SYNAPTICS)
		send_command_to_trackpoint(TP_TOGGLE_SOURCE_TAG,
			TP_TOGGLE_SNAPTICS_SLEEP);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, ps2_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void ps2_resume(void)
{
	int trackpoint_id;

	trackpoint_id = get_trackpoint_id();
	ps2_enable_channel(PRIMUS_PS2_CH, 1, send_aux_data_to_host_interrupt);
	/*
	 * For Synaptics trackpoint, EC need to send command to it again.
	 * For Elan trackpoint, we just need to touch trackpoint and it wake.
	 */
	if (trackpoint_id == TP_VARIANT_SYNAPTICS)
		send_command_to_trackpoint(TP_TOGGLE_SOURCE_TAG,
			TP_TOGGLE_SNAPTICS_SLEEP);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, ps2_resume, HOOK_PRIO_DEFAULT);
