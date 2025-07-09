/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Frostflow board-specific USB-C mux configuration */

#include "ap_power/ap_power.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/ps8811.h"
#include "hooks.h"
#include "i2c.h"
#include "i2c/i2c.h"
#include "ioexpander.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

struct ps8811_reg_val {
	uint8_t reg;
	uint16_t val;
};

/*
 * USB C0 (general) and C1 (just ps8815 DB) use IOEX pins to
 * indicate flipped polarity to a protection switch.
 */
test_export_static int ioex_set_flip(int port, mux_state_t mux_state)
{
	if (port == 0) {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				0);
	} else {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				0);
	}

	return EC_SUCCESS;
}

int board_c0_amd_fp6_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	return EC_SUCCESS;
}

int board_c1_ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	CPRINTSUSB("C1: PS8818 mux using default tuning");

	/* Once a DP connection is established, we need to set IN_HPD */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return 0;
}

const static struct ps8811_reg_val equalizer_wwan_table[] = {
	{
		/* Set channel A EQ setting */
		.reg = PS8811_REG1_USB_AEQ_LEVEL,
		.val = (PS8811_AEQ_I2C_LEVEL_UP_13DB
			<< PS8811_AEQ_I2C_LEVEL_UP_SHIFT) |
		       (PS8811_AEQ_PIN_LEVEL_UP_18DB
			<< PS8811_AEQ_PIN_LEVEL_UP_SHIFT),
	},
	{
		/* Set ADE pin setting */
		.reg = PS8811_REG1_USB_ADE_CONFIG,
		.val = (PS8811_ADE_PIN_MID_LEVEL_3DB
			<< PS8811_ADE_PIN_MID_LEVEL_SHIFT) |
		       PS8811_AEQ_CONFIG_REG_ENABLE,
	},
	{
		/* Set channel B EQ setting */
		.reg = PS8811_REG1_USB_BEQ_LEVEL,
		.val = (PS8811_BEQ_I2C_LEVEL_UP_10P5DB
			<< PS8811_BEQ_I2C_LEVEL_UP_SHIFT) |
		       (PS8811_BEQ_PIN_LEVEL_UP_18DB
			<< PS8811_BEQ_PIN_LEVEL_UP_SHIFT),
	},
	{
		/* Set BDE pin setting */
		.reg = PS8811_REG1_USB_BDE_CONFIG,
		.val = (PS8811_BDE_PIN_MID_LEVEL_3DB
			<< PS8811_BDE_PIN_MID_LEVEL_SHIFT) |
		       PS8811_BEQ_CONFIG_REG_ENABLE,
	},
};

#define NUM_EQ_WWAN_ARRAY ARRAY_SIZE(equalizer_wwan_table)

const static struct ps8811_reg_val equalizer_wlan_table[] = {
	{
		/* Set 50ohm adjust for B channel */
		.reg = PS8811_REG1_50OHM_ADJUST_CHAN_B,
		.val = (PS8811_50OHM_ADJUST_CHAN_B_MINUS_14PCT
			<< PS8811_50OHM_ADJUST_CHAN_B_SHIFT),
	},
};

#define NUM_EQ_WLAN_ARRAY ARRAY_SIZE(equalizer_wlan_table)
/* USB-A ports */
enum usba_port { USBA_PORT_A1, USBA_PORT_COUNT };
const struct usb_mux usba_ps8811[] = {
	[USBA_PORT_A1] = {
		.usb_port = USBA_PORT_A1,
		.i2c_port = I2C_PORT_NODELABEL(i2c1_0),
		.i2c_addr_flags = PS8811_I2C_ADDR_FLAGS3,
	},
};

static int usba_retimer_init(int port)
{
	int rv;
	int val;
	int i;
	const struct usb_mux *me = &usba_ps8811[port];

	rv = ps8811_i2c_read(me, PS8811_REG_PAGE1, PS8811_REG1_USB_BEQ_LEVEL,
			     &val);

	if (rv) {
		CPRINTSUSB("A1: PS8811 retimer response fail!");
		return rv;
	}
	CPRINTSUSB("A1: PS8811 retimer detected");

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* Set channel A output swing */
		rv = ps8811_i2c_field_update(me, PS8811_REG_PAGE1,
					     PS8811_REG1_USB_CHAN_A_SWING,
					     PS8811_CHAN_A_SWING_MASK,
					     0x3 << PS8811_CHAN_A_SWING_SHIFT);

		for (i = 0; i < NUM_EQ_WWAN_ARRAY; i++)
			rv |= ps8811_i2c_write(me, PS8811_REG_PAGE1,
					       equalizer_wwan_table[i].reg,
					       equalizer_wwan_table[i].val);
		for (i = 0; i < NUM_EQ_WLAN_ARRAY; i++)
			rv |= ps8811_i2c_write(me, PS8811_REG_PAGE1,
					       equalizer_wlan_table[i].reg,
					       equalizer_wlan_table[i].val);
	}
	return rv;
}

void baseboard_a1_retimer_setup(void)
{
	int i;

	for (i = 0; i < USBA_PORT_COUNT; ++i)
		usba_retimer_init(i);
}
DECLARE_DEFERRED(baseboard_a1_retimer_setup);

test_export_static void board_resume_change(struct ap_power_ev_callback *cb,
					    struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_STARTUP:
		/* Any retimer tuning can be done after the retimer turns on */
		hook_call_deferred(&baseboard_a1_retimer_setup_data,
				   20 * USEC_PER_MSEC);
		break;
	}
}

void board_callback_init(void)
{
	static struct ap_power_ev_callback cb;

	/* Setup a resume callback */
	ap_power_ev_init_callback(&cb, board_resume_change, AP_POWER_STARTUP);
	ap_power_ev_add_callback(&cb);
}
DECLARE_HOOK(HOOK_INIT, board_callback_init, HOOK_PRIO_DEFAULT);
