/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Dojo board configuration */

#include "cbi_fw_config.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_bmi_common_public.h"
#include "driver/accelgyro_bmi260_public.h"
#include "driver/retimer/ps8802.h"
#include "driver/usb_mux/anx3443.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "motion_sense.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"
#include "usb_mux.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

uint32_t board_version;

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Increase from 50 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/* Vol-up key matrix at T13 */
const struct vol_up_key vol_up_key_matrix_T13 = {
	.row = 3,
	.col = 5,
};

/* Vol-up key matrix at T12 */
const struct vol_up_key vol_up_key_matrix_T12 = {
	.row = 1,
	.col = 5,
};

/* Vol-up key update */
static void board_update_vol_up_key(void)
{
	if (board_version >= 2) {
		if (get_cbi_fw_config_kblayout() == KB_BL_TOGGLE_KEY_PRESENT) {
			/*
			 * Set vol up key to T13 for KB_BL_TOGGLE_KEY_PRESENT
			 * and board_version >= 2
			 */
			set_vol_up_key(vol_up_key_matrix_T13.row, vol_up_key_matrix_T13.col);
		} else {
			/*
			 * Set vol up key to T12 for KB_BL_TOGGLE_KEY_ABSENT
			 * and board_version >= 2
			 */
			set_vol_up_key(vol_up_key_matrix_T12.row, vol_up_key_matrix_T12.col);
		}
	} else {
		/* Set vol up key to T13 for board_version < 2 */
		set_vol_up_key(vol_up_key_matrix_T13.row, vol_up_key_matrix_T13.col);
	}
}

/* Temperature charging table */
const struct temp_chg_struct temp_chg_table[] = {
	[LEVEL_0] = {
		.lo_thre = 0,
		.hi_thre = 68,
		.chg_curr = 3000,
	},
	[LEVEL_1] = {
		.lo_thre = 63,
		.hi_thre = 74,
		.chg_curr = 1500,
	},
	[LEVEL_2] = {
		.lo_thre = 69,
		.hi_thre = 100,
		.chg_curr = 500,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_chg_table) == CHG_LEVEL_COUNT);

/* Sensor */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct icm_drv_data_t g_icm426xx_data;
static struct bmi_drv_data_t g_bmi260_data;
static struct kionix_accel_data g_kx022_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

static const mat33_fp_t bmi260_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs. */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
		.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM426XX_GYRO_MIN_FREQ,
		.max_frequency = ICM426XX_GYRO_MAX_FREQ,
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2, /* g, enough for laptop. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			 /* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t bmi260_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.rot_standard_ref = &bmi260_standard_ref,
	.min_frequency = BMI_ACCEL_MIN_FREQ,
	.max_frequency = BMI_ACCEL_MAX_FREQ,
	.default_range = 4, /* g */
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
	},
};

struct motion_sensor_t bmi260_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &bmi260_standard_ref,
	.min_frequency = BMI_GYRO_MIN_FREQ,
	.max_frequency = BMI_GYRO_MAX_FREQ,
};

static void board_update_motion_sensor_config(void)
{
	if (board_version >= 2) {
		motion_sensors[BASE_ACCEL] = bmi260_base_accel;
		motion_sensors[BASE_GYRO] = bmi260_base_gyro;
		ccprints("BASE Accelgyro is BMI260");
	} else {
		ccprints("BASE Accelgyro is ICM426XX");
	}
}

void motion_interrupt(enum gpio_signal signal)
{
	if (board_version >= 2)
		bmi260_interrupt(signal);
	else
		icm426xx_interrupt(signal);
}

/* PWM */

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_C1_WHITE] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_C1_AMBER] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_PWR] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq_hz = 10000, /* SYV226 supports 10~100kHz */
		.pcfsr_sel = PWM_PRESCALER_C6,
	},
	[PWM_CH_LED_C0_WHITE] = {
		.channel = 6,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_C0_AMBER] = {
		.channel = 7,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* USB Mux */

static int board_ps8762_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	/* Make sure the PS8802 is awake */
	RETURN_ERROR(ps8802_i2c_wake(me));

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		RETURN_ERROR(ps8802_i2c_field_update16(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_USB_SSEQ_LEVEL,
					PS8802_USBEQ_LEVEL_UP_MASK,
					PS8802_USBEQ_LEVEL_UP_12DB));
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		RETURN_ERROR(ps8802_i2c_field_update8(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_DPEQ_LEVEL,
					PS8802_DPEQ_LEVEL_UP_MASK,
					PS8802_DPEQ_LEVEL_UP_9DB));
	}

	return EC_SUCCESS;
}

static int board_ps8762_mux_init(const struct usb_mux *me)
{
	return ps8802_i2c_field_update8(
			me, PS8802_REG_PAGE1,
			PS8802_REG_DCIRX,
			PS8802_AUTO_DCI_MODE_DISABLE | PS8802_FORCE_DCI_MODE,
			PS8802_AUTO_DCI_MODE_DISABLE);
}

static int board_anx3443_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	gpio_set_level(GPIO_USB_C1_DP_IN_HPD,
		       mux_state & USB_PD_MUX_DP_ENABLED);
	return EC_SUCCESS;
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX0,
		.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS,
		.driver = &ps8802_usb_mux_driver,
		.board_init = &board_ps8762_mux_init,
		.board_set = &board_ps8762_mux_set,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX1,
		.i2c_addr_flags = ANX3443_I2C_ADDR0_FLAGS,
		.driver = &anx3443_usb_mux_driver,
		.board_set = &board_anx3443_mux_set,
	},
};

/* Initialize board. */
static void board_init(void)
{
	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
	gpio_enable_interrupt(GPIO_LID_ACCEL_INT_L);

	/* Store board version for use of something */
	cbi_get_board_version(&board_version);

	board_update_motion_sensor_config();
	board_update_vol_up_key();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_do_chipset_resume(void)
{
	gpio_set_level(GPIO_EN_PP3300_SSD, 1);
	gpio_set_level(GPIO_EN_KB_BL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_do_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_do_chipset_suspend(void)
{
	gpio_set_level(GPIO_EN_PP3300_SSD, 0);
	gpio_set_level(GPIO_EN_KB_BL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_do_chipset_suspend, HOOK_PRIO_DEFAULT);
