/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lazor board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "extpower.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/ln9310.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "mkbp_info.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"
#include "shi_chip.h"
#include "sku.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

/* Disable debug messages to free flash space */
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)

#include "gpio_list.h"

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Use 80 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/*
	 * Unmask 0x08 in [0] (KSO_00/KSI_03, the new location of Search key);
	 * as it still uses the legacy location (KSO_01/KSI_00).
	 */
	.actual_key_mask = { 0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff, 0xa4,
			     0xff, 0xfe, 0x55, 0xfa, 0xca },
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
};

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 },   { 1, 0 },	{ 0, 6 },   { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 },   { 1, 3 },	{ -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 },   { 1, 5 },	{ 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 },   { 1, 2 },	{ 2, 3 },   { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
	{ -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C_POWER_SCL,
	  .sda = GPIO_EC_I2C_POWER_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_USB_C0_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C0_PD_SDA },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_USB_C1_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C1_PD_SDA },
	{ .name = "eeprom",
	  .port = I2C_PORT_EEPROM,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_EEPROM_SCL,
	  .sda = GPIO_EC_I2C_EEPROM_SDA },
	{ .name = "sensor",
	  .port = I2C_PORT_SENSOR,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_SENSOR_SCL,
	  .sda = GPIO_EC_I2C_SENSOR_SDA },
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Measure VBUS through a 1/10 voltage divider */
	[ADC_VBUS] = { "VBUS", NPCX_ADC_CH1, ADC_MAX_VOLT * 10,
		       ADC_READ_MAX + 1, 0 },
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = { "AMON_BMON", NPCX_ADC_CH2, ADC_MAX_VOLT * 1000 / 18,
			    ADC_READ_MAX + 1, 0 },
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 5.6K resistor, to read
	 * 0.8V @ 99 W, i.e. 124000 uW/mV. Using ADC_MAX_VOLT*124000 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = { "PSYS", NPCX_ADC_CH3,
		       ADC_MAX_VOLT * 124000 * 2 / (ADC_READ_MAX + 1), 2, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	/* TODO(waihong): Assign a proper frequency. */
	[PWM_CH_DISPLIGHT] = { .channel = 5, .flags = 0, .freq = 4800 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Mutexes */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;
static struct accelgyro_saved_data_t g_bma255_data;

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI160 = 1,
	BASE_GYRO_ICM426XX = 2,
};

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref_bmi160 = { { FLOAT_TO_FP(1), 0, 0 },
					      { 0, FLOAT_TO_FP(-1), 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t base_standard_ref_icm426xx = { { 0, FLOAT_TO_FP(1), 0 },
						{ FLOAT_TO_FP(1), 0, 0 },
						{ 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t lid_standard_ref_bma255 = { { FLOAT_TO_FP(-1), 0, 0 },
						    { 0, FLOAT_TO_FP(-1), 0 },
						    { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t lid_standard_ref_kx022 = { { FLOAT_TO_FP(-1), 0, 0 },
						   { 0, FLOAT_TO_FP(-1), 0 },
						   { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMA255,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bma2x2_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bma255_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref_bma255,
	 .default_range = 2, /* g, to support lid angle calculation. */
	 .min_frequency = BMA255_ACCEL_MIN_FREQ,
	 .max_frequency = BMA255_ACCEL_MAX_FREQ,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &base_standard_ref_bmi160,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	[BASE_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref_bmi160,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t kx022_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_kx022_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = KX022_ADDR0_FLAGS,
	.rot_standard_ref = &lid_standard_ref_kx022,
	.default_range = 2, /* g, enough for laptop. */
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		 },
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	.rot_standard_ref = &base_standard_ref_icm426xx,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3_S5,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref_icm426xx,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

static int base_accelgyro_config;

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_accelgyro_config) {
	case BASE_GYRO_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case BASE_GYRO_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

static void board_detect_motionsensor(void)
{
	int ret;
	int val;

	/* Check lid accel chip */
	ret = i2c_read8(I2C_PORT_SENSOR, BMA2x2_I2C_ADDR1_FLAGS,
			BMA2x2_CHIP_ID_ADDR, &val);
	if (ret)
		motion_sensors[LID_ACCEL] = kx022_lid_accel;

	CPRINTS("Lid Accel: %s", ret ? "KX022" : "BMA255");

	/* Check base accelgyro chip */
	ret = icm_read8(&icm426xx_base_accel, ICM426XX_REG_WHO_AM_I, &val);
	if (val == ICM426XX_CHIP_ICM40608) {
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
	}

	base_accelgyro_config = (val == ICM426XX_CHIP_ICM40608) ?
					BASE_GYRO_ICM426XX :
					BASE_GYRO_BMI160;
	CPRINTS("Base Accelgyro: %s",
		(val == ICM426XX_CHIP_ICM40608) ? "ICM40608" : "BMI160");
}

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_clamshell()) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* The sensors are not stuffed; don't allow lines to float */
		gpio_set_flags(GPIO_ACCEL_GYRO_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_LID_ACCEL_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	} else {
		board_detect_motionsensor();
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable interrupt for the base accel sensor */
		gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);
	}
}
DECLARE_HOOK(HOOK_INIT, board_update_sensor_config_from_sku,
	     HOOK_PRIO_INIT_I2C + 2);

/* Initialize board. */
static void board_init(void)
{
	/* Set the backlight duty cycle to 0. AP will override it later. */
	pwm_set_duty(PWM_CH_DISPLIGHT, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/*
	 * Turn off display backlight in S3. AP has its own control. The EC's
	 * and the AP's will be AND'ed together in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
	pwm_enable(PWM_CH_DISPLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Turn on display and keyboard backlight in S0. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	if (pwm_get_duty(PWM_CH_DISPLIGHT))
		pwm_enable(PWM_CH_DISPLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

__override uint32_t board_get_sku_id(void)
{
	static int sku_id = -1;

	if (sku_id == -1) {
		int bits[3];

		bits[0] = gpio_get_ternary(GPIO_SKU_ID0);
		bits[1] = gpio_get_ternary(GPIO_SKU_ID1);
		bits[2] = gpio_get_ternary(GPIO_SKU_ID2);
		sku_id = binary_first_base3_from_bits(bits, ARRAY_SIZE(bits));
	}

	return (uint32_t)sku_id;
}

__override int mkbp_support_volume_buttons(void)
{
	return board_has_side_volume_buttons();
}
