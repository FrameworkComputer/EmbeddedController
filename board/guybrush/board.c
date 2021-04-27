/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "button.h"
#include "common.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/retimer/ps8818.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "usb_mux.h"

#include "gpio_list.h" /* Must come after other header files. */

/* Lid Sensor mutex */
static struct mutex g_base_mutex;

/* Lid accel private data */
static struct bmi_drv_data_t g_bmi160_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void board_init(void)
{
	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 * TODO(b/179036200): Adjust PS8818 tuning for guybrush reference
 */
__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				    mux_state_t mux_state)
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

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_RX_PHY,
					PS8818_RX_INPUT_TERM_MASK,
					PS8818_RX_INPUT_TERM_112_OHM);
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

		/* Enable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		gpio_set_level(GPIO_USB_C1_HPD, 0);
	}

	return rv;
}

/*
 * ANX7491(A1) and ANX7451(C1) are on the same i2c bus. Both default
 * to 0x29 for the USB i2c address. This moves ANX7451(C1) USB i2c
 * address to 0x2A. ANX7491(A1) will stay at the default 0x29.
 */
uint16_t board_anx7451_get_usb_i2c_addr(const struct usb_mux *me)
{
	ASSERT(me->usb_port == USBC_PORT_C1);
	return 0x2a;
}

void motion_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_3AXIS_INT_L:
		/* TODO */
		break;
	case GPIO_6AXIS_INT_L:
	default:
		bmi160_interrupt(signal);
		break;
	}
}
