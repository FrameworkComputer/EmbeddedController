/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/retimer/ps8802.h"
#include "driver/retimer/ps8818.h"
#include "driver/usb_mux/amd_fp5.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "timer.h"
#include "usb_mux.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/*****************************************************************************
 * Fan
 */

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3100,
	.rpm_start = 3100,
	.rpm_max = 6900,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

const static struct ec_thermal_config thermal_thermistor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * MST hub
 */

static void mst_hpd_handler(void)
{
	int hpd = 0;

	/* Pass HPD through from DB OPT3 MST hub to AP's DP1. */
	ioex_get_level(IOEX_MST_HPD_OUT, &hpd);
	gpio_set_level(GPIO_DP1_HPD, hpd);
	ccprints("MST HPD %d", hpd);
}
DECLARE_DEFERRED(mst_hpd_handler);

void mst_hpd_interrupt(enum ioex_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&mst_hpd_handler_data, (2 * MSEC));
}

/*****************************************************************************
 * Custom Zork USB-C1 Retimer/MUX driver
 */

/*
 * PS8802 set mux tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int ps8802_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* Make sure the PS8802 is awake */
	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8802_i2c_field_update16(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_USB_SSEQ_LEVEL,
					PS8802_USBEQ_LEVEL_UP_MASK,
					PS8802_USBEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8802_i2c_field_update8(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_DPEQ_LEVEL,
					PS8802_DPEQ_LEVEL_UP_MASK,
					PS8802_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}

	return rv;
}

/*
 * PS8818 set mux tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int ps8818_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_DPEQ_LEVEL,
					PS8818_DPEQ_LEVEL_UP_MASK,
					PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}

	return rv;
}

/*
 * To support both OPT1 DB with PS8818 retimer, and OPT3 DB with PS8802
 * retimer,  Try both, and remember the first one that succeeds.
 */
const struct usb_mux usbc1_ps8802;
const struct usb_mux usbc1_ps8818;
struct usb_mux usbc1_amd_fp5_usb_mux;

enum zork_c1_retimer zork_c1_retimer = C1_RETIMER_UNKNOWN;
static int zork_c1_detect(const struct usb_mux *me, int err_if_power_off)
{
	int rv;

	/*
	 * Retimers are not powered in G3 so return success if setting mux to
	 * none and error otherwise.
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (err_if_power_off) ? EC_ERROR_NOT_POWERED
					  : EC_SUCCESS;

	/*
	 * Identifying a PS8818 is faster than the PS8802,
	 * so do it first.
	 */
	rv = ps8818_detect(&usbc1_ps8818);
	if (rv == EC_SUCCESS) {
		zork_c1_retimer = C1_RETIMER_PS8818;
		ccprints("C1 PS8818 detected");

		/* Main MUX is FP5, secondary MUX is PS8818 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_amd_fp5_usb_mux,
		       sizeof(struct usb_mux));
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_ps8818;
		return rv;
	}

	rv = ps8802_detect(&usbc1_ps8802);
	if (rv == EC_SUCCESS) {
		zork_c1_retimer = C1_RETIMER_PS8802;
		ccprints("C1 PS8802 detected");

		/* Main MUX is PS8802, secondary MUX is modified FP5 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_ps8802,
		       sizeof(struct usb_mux));
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_amd_fp5_usb_mux;
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;
	}

	return rv;
}

/*
 * We start off not sure which configuration we are using.  We set
 * the interface to be this special primary MUX driver in order to
 * determine the actual hardware and then we patch the jump tables
 * to go to the actual drivers instead.
 *
 * "me" will always point to usb_muxes[0].  If detection is made
 * on the real device, then detect will change the tables so the
 * content of me is the real driver configuration and will setup
 * next_mux appropriately. So all we have to do on detection is
 * perform the actual call for this entry and then let the generic
 * chain traverse mechanism in usb_mux.c do any following calls.
 */
static int zork_c1_init_mux(const struct usb_mux *me)
{
	int rv;

	/* Try to detect, but don't give an error if no power */
	rv = zork_c1_detect(me, 0);
	if (rv)
		return rv;

	/*
	 * If we detected the hardware, then call the real routine.
	 * We only do this one time, after that time we will go direct
	 * and avoid this special driver.
	 */
	if (zork_c1_retimer != C1_RETIMER_UNKNOWN)
		if (me->driver && me->driver->init)
			rv = me->driver->init(me);

	return rv;
}

static int zork_c1_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv;

	/*
	 * Try to detect, give an error if we are setting to a
	 * MUX value that is not NONE when we have no power.
	 */
	rv = zork_c1_detect(me, mux_state != USB_PD_MUX_NONE);
	if (rv)
		return rv;

	/*
	 * If we detected the hardware, then call the real routine.
	 * We only do this one time, after that time we will go direct
	 * and avoid this special driver.
	 */
	if (zork_c1_retimer != C1_RETIMER_UNKNOWN) {
		const struct usb_mux_driver *drv = me->driver;

		if (drv && drv->set) {
			mux_state_t state = mux_state;

			if (me->flags & USB_MUX_FLAG_SET_WITHOUT_FLIP)
				state &= ~USB_PD_MUX_POLARITY_INVERTED;

			/* Apply Driver generic settings */
			rv = drv->set(me, state);
			if (rv)
				return rv;

			/* Apply Board specific settings */
			if (me->board_set)
				rv = me->board_set(me, state);
		}
	}

	return rv;
}

static int zork_c1_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int rv;

	/* Try to detect the hardware */
	rv = zork_c1_detect(me, 1);
	if (rv) {
		/*
		 * Not powered is MUX_NONE, so change the values
		 * and make it a good status
		 */
		if (rv == EC_ERROR_NOT_POWERED) {
			*mux_state = USB_PD_MUX_NONE;
			rv = EC_SUCCESS;
		}
		return rv;
	}

	/*
	 * If we detected the hardware, then call the real routine.
	 * We only do this one time, after that time we will go direct
	 * and avoid this special driver.
	 */
	if (zork_c1_retimer != C1_RETIMER_UNKNOWN)
		if (me->driver && me->driver->get)
			rv = me->driver->get(me, mux_state);

	return rv;
}

const struct pi3dpx1207_usb_control pi3dpx1207_controls[] = {
	[USBC_PORT_C0] = {
#ifdef VARIANT_ZORK_TREMBYLE
		.enable_gpio = IOEX_USB_C0_DATA_EN,
		.dp_enable_gpio = GPIO_USB_C0_IN_HPD,
#endif
	},
	[USBC_PORT_C1] = {
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3dpx1207_controls) == USBC_PORT_COUNT);

const struct usb_mux_driver zork_c1_usb_mux_driver = {
	.init = zork_c1_init_mux,
	.set = zork_c1_set_mux,
	.get = zork_c1_get_mux,
};

const struct usb_mux usbc0_pi3dpx1207_usb_retimer = {
	.usb_port = USBC_PORT_C0,
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = PI3DPX1207_I2C_ADDR_FLAGS,
	.driver = &pi3dpx1207_usb_retimer,
};

const struct usb_mux usbc1_ps8802 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS,
	.driver = &ps8802_usb_mux_driver,
	.board_set = &ps8802_mux_set,
};
const struct usb_mux usbc1_ps8818 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &ps8818_mux_set,
};
struct usb_mux usbc1_amd_fp5_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_USB_AP_MUX,
	.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
	.driver = &amd_fp5_usb_mux_driver,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
		.next_mux = &usbc0_pi3dpx1207_usb_retimer,
	},
	[USBC_PORT_C1] = {
		/*
		 * This is the detection driver. Once the hardware
		 * has been detected, the driver will change to the
		 * detected hardware driver table.
		 */
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_TCPC1,
		.driver = &zork_c1_usb_mux_driver,
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);
