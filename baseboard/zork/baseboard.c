/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zork family-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpci.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct i2c_port_t i2c_ports[] = {
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_I2C0_SCL,
		.sda = GPIO_I2C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_I2C1_SCL,
		.sda = GPIO_I2C1_SDA,
	},
	{
		.name = "power",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_I2C2_SCL,
		.sda = GPIO_I2C2_SDA,
	},
	{
		.name = "mux",
		.port = I2C_PORT_MUX,
		.kbps = 400,
		.scl = GPIO_I2C3_SCL,
		.sda = GPIO_I2C3_SDA,
	},
	{
		.name = "thermal",
		.port = I2C_PORT_THERMAL,
		.kbps = 400,
		.scl = GPIO_I2C4_SCL,
		.sda = GPIO_I2C4_SDA,
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_I2C5_SCL,
		.sda = GPIO_I2C5_SDA,
	},
	{
		.name = "ap_audio",
		.port = I2C_PORT_AP_AUDIO,
		.kbps = 400,
		.scl = GPIO_I2C6_SCL,
		.sda = GPIO_I2C6_SDA,
	},
	{
		.name = "ap_hdmi",
		.port = I2C_PORT_AP_HDMI,
		.kbps = 400,
		.scl = GPIO_I2C7_SCL,
		.sda = GPIO_I2C7_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

#define USB_PD_PORT_TCPC_0	0
#define USB_PD_PORT_TCPC_1	1

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = PS8751_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	[USB_PD_PORT_TCPC_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8751_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
	[USB_PD_PORT_TCPC_1] = {
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
};

static void baseboard_chipset_suspend(void)
{
	/* Disable display and keyboard backlights. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 1);
	/* TODO gpio_set_level(GPIO_KB_BL_EN, 0); */
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

static void baseboard_chipset_resume(void)
{
	/* Enable display and keyboard backlights. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 0);
	/* TODO gpio_set_level(GPIO_KB_BL_EN, 1); */
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Extra delay when KSO2 is tied to Cr50. */
	.output_settle_us = 60,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/*
 * We use 11 as the scaling factor so that the maximum mV value below (2761)
 * can be compressed to fit in a uint8_t.
 */
#define THERMISTOR_SCALING_FACTOR 11

/*
 * Values are calculated from the "Resistance VS. Temperature" table on the
 * Murata page for part NCP15WB473F03RC. Vdd=3.3V, R=30.9Kohm.
 */
static const struct thermistor_data_pair thermistor_data[] = {
	{ 2761 / THERMISTOR_SCALING_FACTOR, 0},
	{ 2492 / THERMISTOR_SCALING_FACTOR, 10},
	{ 2167 / THERMISTOR_SCALING_FACTOR, 20},
	{ 1812 / THERMISTOR_SCALING_FACTOR, 30},
	{ 1462 / THERMISTOR_SCALING_FACTOR, 40},
	{ 1146 / THERMISTOR_SCALING_FACTOR, 50},
	{ 878 / THERMISTOR_SCALING_FACTOR, 60},
	{ 665 / THERMISTOR_SCALING_FACTOR, 70},
	{ 500 / THERMISTOR_SCALING_FACTOR, 80},
	{ 434 / THERMISTOR_SCALING_FACTOR, 85},
	{ 376 / THERMISTOR_SCALING_FACTOR, 90},
	{ 326 / THERMISTOR_SCALING_FACTOR, 95},
	{ 283 / THERMISTOR_SCALING_FACTOR, 100}
};

static const struct thermistor_info thermistor_info = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(thermistor_data),
	.data = thermistor_data,
};

static int board_get_temp(int idx, int *temp_k)
{
	/* idx is the sensor index set below in temp_sensors[] */
	int mv = adc_read_channel(
		idx ? ADC_TEMP_SENSOR_SOC : ADC_TEMP_SENSOR_CHARGER);
	int temp_c;

	if (mv < 0)
		return -1;

	temp_c = thermistor_linear_interpolate(mv, &thermistor_info);
	*temp_k = C_TO_K(temp_c);
	return 0;
}

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = 0,
		.action_delay_sec = 1,
	},
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = 1,
		.action_delay_sec = 5,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
		.action_delay_sec = 4,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

mat33_fp_t zork_base_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(1),  0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi160_drv_data_t g_bmi160_data;

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KX022,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kx022_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	 .rot_standard_ref = (const mat33_fp_t *)&lid_standard_ref,
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
	 .default_range = 2, /* g, enough for laptop */
	 .rot_standard_ref = (const mat33_fp_t *)&zork_base_standard_ref,
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
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
	 .rot_standard_ref = (const mat33_fp_t *)&zork_base_standard_ref,
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#endif /* HAS_TASK_MOTIONSENSE */

#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	if (board_is_convertible())
		keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif

static void cbi_init(void)
{
	uint32_t board_version = 0;
	uint32_t sku_id = 0;
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS)
		board_version = val;
	ccprints("Board Version: %d (0x%x)", board_version, board_version);

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku_id = val;
	ccprints("SKU: %d (0x%x)", sku_id, sku_id);

#ifdef HAS_TASK_MOTIONSENSE
	board_update_sensor_config_from_sku();
#endif

}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

/*
 * Returns 1 for boards that are convertible into tablet mode, and zero for
 * clamshells.
 */
int board_is_convertible(void)
{
	/* TODO: Add convertible SKU values */
	return 0;
}

int board_is_lid_angle_tablet_mode(void)
{
	return board_is_convertible();
}

uint32_t board_override_feature_flags0(uint32_t flags0)
{
	return flags0;
}

uint32_t board_override_feature_flags1(uint32_t flags1)
{
	return flags1;
}
