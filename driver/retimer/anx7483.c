/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7483: Active redriver with linear equilzation
 */

#include "anx7483.h"
#include "retimer/anx7483_public.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "usb_mux.h"
#include "util.h"

/*
 * Programming guide specifies it may be as much as 30ms after chip power on
 * before it's ready for i2c
 */
#define ANX7483_I2C_WAKE_TIMEOUT_MS 30
#define ANX7483_I2C_WAKE_RETRY_DELAY_US 5000

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Tuning defaults */
struct anx7483_tuning_set {
	uint8_t addr;
	uint8_t value;
};

const static struct anx7483_tuning_set anx7483_usb_enabled[] = {
	{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

	{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

	{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
	{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
	{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
	{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

	{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

	{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

	{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
	{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
	{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
	{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },

	{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
};

static struct anx7483_tuning_set anx7483_dp_enabled[] = {
	{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

	{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

	{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

	{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

	{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

	{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
};

static struct anx7483_tuning_set anx7483_dock_noflip[] = {
	{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

	{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

	{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

	{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
	{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

	{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

	{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

	{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
};

static struct anx7483_tuning_set anx7483_dock_flip[] = {
	{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

	{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
	{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },

	{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
	{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },

	{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
	{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

	{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
	{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

	{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
	{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

	{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
};

static inline int anx7483_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static inline int anx7483_write(const struct usb_mux *me, uint8_t reg,
				uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int anx7483_init(const struct usb_mux *me)
{
	timestamp_t start;
	int rv;
	int val;

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	/* Keep reading control register until mux wakes up or times out */
	start = get_time();
	do {
		rv = anx7483_read(me, ANX7483_ANALOG_STATUS_CTRL_REG, &val);
		if (!rv)
			break;
		usleep(ANX7483_I2C_WAKE_RETRY_DELAY_US);
	} while (time_since32(start) < ANX7483_I2C_WAKE_TIMEOUT_MS * MSEC);

	if (rv) {
		CPRINTS("ANX7483: Failed to wake mux rv:%d", rv);
		return EC_ERROR_TIMEOUT;
	}

	/* Configure for i2c control */
	val |= ANX7483_CTRL_REG_EN;
	RETURN_ERROR(anx7483_write(me, ANX7483_ANALOG_STATUS_CTRL_REG, val));

	return EC_SUCCESS;
}

static int anx7483_set(const struct usb_mux *me, mux_state_t mux_state,
		       bool *ack_required)
{
	int reg;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state == USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	/*
	 * Mux is not powered in Z1
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	/*
	 * Always ensure i2c control is set and state machine is enabled
	 * (setting ANX7483_CTRL_REG_BYPASS_EN disables state machine)
	 */
	reg = ANX7483_CTRL_REG_EN;
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= ANX7483_CTRL_USB_EN;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= ANX7483_CTRL_DP_EN;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= ANX7483_CTRL_FLIP_EN;

	return anx7483_write(me, ANX7483_ANALOG_STATUS_CTRL_REG, reg);
}

static int anx7483_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return USB_PD_MUX_NONE;

	*mux_state = 0;
	RETURN_ERROR(anx7483_read(me, ANX7483_ANALOG_STATUS_CTRL_REG, &reg));

	if (reg & ANX7483_CTRL_USB_EN)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & ANX7483_CTRL_DP_EN)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & ANX7483_CTRL_FLIP_EN)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/* Helper to apply entire array of tuning registers, returning on first error */
static enum ec_error_list
anx7483_apply_tuning(const struct usb_mux *me,
		     const struct anx7483_tuning_set *reg, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		RETURN_ERROR(anx7483_write(me, reg[i].addr, reg[i].value));
	}

	return EC_SUCCESS;
}

int anx7483_set_default_tuning(const struct usb_mux *me, mux_state_t mux_state)
{
	bool flipped = mux_state & USB_PD_MUX_POLARITY_INVERTED;

	/* Remove flipped from the state for easier compraisons */
	mux_state = mux_state & ~USB_PD_MUX_POLARITY_INVERTED;

	/* Enable i2c configuration of tuning registers */
	RETURN_ERROR(anx7483_write(me, ANX7483_ENABLE_EQ_FLAT_SWING_REG,
				   ANX7483_ENABLE_EQ_FLAT_SWING_EN));

	if (mux_state == USB_PD_MUX_USB_ENABLED) {
		return anx7483_apply_tuning(me, anx7483_usb_enabled,
					    ARRAY_SIZE(anx7483_usb_enabled));
	} else if (mux_state == USB_PD_MUX_DP_ENABLED) {
		return anx7483_apply_tuning(me, anx7483_dp_enabled,
					    ARRAY_SIZE(anx7483_dp_enabled));
	} else if (mux_state == USB_PD_MUX_DOCK && !flipped) {
		return anx7483_apply_tuning(me, anx7483_dock_noflip,
					    ARRAY_SIZE(anx7483_dock_noflip));
	} else if (mux_state == USB_PD_MUX_DOCK && flipped) {
		return anx7483_apply_tuning(me, anx7483_dock_flip,
					    ARRAY_SIZE(anx7483_dock_flip));
	}

	return EC_SUCCESS;
}

enum ec_error_list anx7483_set_eq(const struct usb_mux *me,
				  enum anx7483_tune_pin pin,
				  enum anx7483_eq_setting eq)
{
	int reg, value;

	if (pin == ANX7483_PIN_UTX1)
		reg = ANX7483_UTX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_UTX2)
		reg = ANX7483_UTX2_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_URX1)
		reg = ANX7483_URX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_URX2)
		reg = ANX7483_URX2_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_DRX1)
		reg = ANX7483_DRX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_DRX2)
		reg = ANX7483_DRX2_PORT_CFG0_REG;
	else
		return EC_ERROR_INVAL;

	RETURN_ERROR(anx7483_read(me, reg, &value));
	value &= ~ANX7483_CFG0_EQ_MASK;
	value |= eq << ANX7483_CFG0_EQ_SHIFT;

	return anx7483_write(me, reg, value);
}

const struct usb_mux_driver anx7483_usb_retimer_driver = {
	.init = anx7483_init,
	.set = anx7483_set,
	.get = anx7483_get,
};
