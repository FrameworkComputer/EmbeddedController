/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board-specific configuration */

#include "adc.h"
#include "base_fw_config.h"
#include "battery.h"
#include "board_fw_config.h"
#include "builtin/assert.h"
#include "button.h"
#include "charger.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_bma422.h"
#include "driver/accelgyro_bmi260.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/temp_sensor/pct2075.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "temp_sensor/pct2075.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"
#include "usb_mux.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Motion sensor mutex */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Motion sensor private data */
static struct bmi_drv_data_t g_bmi_data;
static struct accelgyro_saved_data_t g_bma422_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(1), 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 }, { 1, 0 },   { 0, 6 },	  { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 }, { 1, 3 },   { -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 }, { 1, 5 },   { 2, 6 },	  { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 }, { 1, 2 },   { 2, 3 },	  { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { 0, 4 }, { -1, -1 }, { 8, 2 },	  { -1, -1 },
	{ -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI260,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi260_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
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
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA422,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma4_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma422_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA4_I2C_ADDR_PRIMARY,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA4_ACCEL_MIN_FREQ,
		.max_frequency = BMA4_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, enough for laptop. */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI260,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi260_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

__override enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	/* Set channel A output swing */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_A_SWING,
		PS8811_CHAN_A_SWING_MASK, 0x2 << PS8811_CHAN_A_SWING_SHIFT));

	/* Set channel B output swing */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_SWING,
		PS8811_CHAN_B_SWING_MASK, 0x2 << PS8811_CHAN_B_SWING_SHIFT));

	/* Set channel B de-emphasis to -6dB and pre-shoot to 1.5 dB */
	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
		PS8811_CHAN_B_DE_PS_LSB_MASK, PS8811_CHAN_B_DE_6_PS_1_5_LSB));

	RETURN_ERROR(ps8811_i2c_field_update(
		me, PS8811_REG_PAGE1, PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
		PS8811_CHAN_B_DE_PS_MSB_MASK, PS8811_CHAN_B_DE_6_PS_1_5_MSB));

	return EC_SUCCESS;
}

__override int board_c1_ps8818_mux_set(const struct usb_mux *me,
				       mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Set the RX input termination */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_RX_PHY,
					      PS8818_RX_INPUT_TERM_MASK,
					      PS8818_RX_INPUT_TERM_112_OHM);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_DPEQ_LEVEL,
					      PS8818_DPEQ_LEVEL_UP_MASK,
					      PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable HPD on the DB */
		ioex_set_level(IOEX_USB_C1_IN_HPD, 1);
	} else {
		/* Disable HPD on the DB */
		ioex_set_level(IOEX_USB_C1_IN_HPD, 0);
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

/*
 * Base Gyro Sensor dynamic configuration
 */
static int base_gyro_config;

static void board_update_motion_sensor_config(void)
{
	if (board_is_convertible()) {
		base_gyro_config = BASE_GYRO_BMI260;
		ccprints("BASE GYRO is BMI260");

		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable Base Accel and Gyro interrupt */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_gyro_config) {
	case BASE_GYRO_BMI260:
	default:
		bmi260_interrupt(signal);
		break;
	}
}

static void board_init(void)
{
	board_update_motion_sensor_config();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	if (get_board_version() > 1)
		pct2075_init();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

int board_get_soc_temp_k(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_k(idx, temp_k);
}

int board_get_soc_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_SOC, temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return pct2075_get_val_mk(PCT2075_AMB, temp_mk);
}

/* ADC Channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_MEMORY] = {
		.name = "MEMORY",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_CORE_IMON1] = {
		.name = "CORE_I",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SOC_IMON2] = {
		.name = "SOC_I",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temp Sensors */
static int board_get_memory_temp(int, int *);

const struct pct2075_sensor_t pct2075_sensors[] = {
	{ I2C_PORT_SENSOR, PCT2075_I2C_ADDR_FLAGS0 },
	{ I2C_PORT_SENSOR, PCT2075_I2C_ADDR_FLAGS7 },
};
BUILD_ASSERT(ARRAY_SIZE(pct2075_sensors) == PCT2075_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_soc_temp_k,
		.idx = PCT2075_SOC,
	},
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_MEMORY] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_memory_temp,
		.idx = ADC_TEMP_SENSOR_MEMORY,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
	[TEMP_SENSOR_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = pct2075_get_val_k,
		.idx = PCT2075_AMB,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static int board_get_memory_temp(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;
	return get_temp_3v3_30k9_47k_4050b(idx, temp_k);
}

/* keyboard config */
static const struct ec_response_keybd_config main_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

/* TK_REFRESH is always T2 above, vivaldi_keys are not overridden. */
BUILD_ASSERT_REFRESH_RC(3, 2);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	return &main_kb;
}

void board_set_current_limit(void)
{
	const int no_battery_current_limit_override_ma = 6000;
	/*
	 * When there is no battery, override charger current limit to
	 * prevent brownout during boot.
	 */
	if (battery_is_present() == BP_NO) {
		ccprints("No Battery Found - Override Current Limit to %dmA",
			 no_battery_current_limit_override_ma);
		charger_set_input_current_limit(
			CHARGER_SOLO, no_battery_current_limit_override_ma);
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, board_set_current_limit,
	     HOOK_PRIO_INIT_EXTPOWER);
