/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Herobrine board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "extpower.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"
#include "shi_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#include "gpio_list.h"

static uint8_t sku_id;

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_RTC_EC_WAKE_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Use 80 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/*
	 * Unmask 0x08 in [0] (KSO_00/KSI_03, the new location of Search key);
	 * as it still uses the legacy location (KSO_01/KSI_00).
	 */
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca
	},
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
};

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  100, GPIO_EC_I2C_POWER_SCL,
					  GPIO_EC_I2C_POWER_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_EC_I2C_USB_C0_PD_SCL,
					  GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_EC_I2C_USB_C1_PD_SCL,
					  GPIO_EC_I2C_USB_C1_PD_SDA},
	{"rtc",     I2C_PORT_RTC,    400, GPIO_EC_I2C_RTC_SCL,
					  GPIO_EC_I2C_RTC_SDA},
	{"eeprom",  I2C_PORT_EEPROM, 400, GPIO_EC_I2C_EEPROM_SCL,
					  GPIO_EC_I2C_EEPROM_SDA},
	{"sensor",  I2C_PORT_SENSOR, 400, GPIO_EC_I2C_SENSOR_SCL,
					  GPIO_EC_I2C_SENSOR_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Measure VBUS through a 1/10 voltage divider */
	[ADC_VBUS] = {
		"VBUS",
		NPCX_ADC_CH1,
		ADC_MAX_VOLT * 10,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {
		"AMON_BMON",
		NPCX_ADC_CH2,
		ADC_MAX_VOLT * 1000 / 18,
		ADC_READ_MAX + 1,
		0
	},
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 5.6K resistor, to read
	 * 0.8V @ 99 W, i.e. 124000 uW/mV. Using ADC_MAX_VOLT*124000 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = {
		"PSYS",
		NPCX_ADC_CH3,
		ADC_MAX_VOLT * 124000 * 2 / (ADC_READ_MAX + 1),
		2,
		0
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	/* TODO(waihong): Assign a proper frequency. */
	[PWM_CH_DISPLIGHT] = { .channel = 5, .flags = 0, .freq = 4800 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Read SKU ID from GPIO and initialize variables for board variants */
static void sku_id_init(void)
{
	int bits[3];

	bits[0] = gpio_get_ternary(GPIO_SKU_ID0);
	bits[1] = gpio_get_ternary(GPIO_SKU_ID1);
	bits[2] = gpio_get_ternary(GPIO_SKU_ID2);

	sku_id = binary_first_base3_from_bits(bits, ARRAY_SIZE(bits));
	CPRINTS("SKU ID: %u", sku_id);
}
DECLARE_HOOK(HOOK_INIT, sku_id_init, HOOK_PRIO_INIT_I2C + 1);

__override uint32_t board_get_sku_id(void)
{
	return sku_id;
}

/* Initialize board. */
static void board_init(void)
{
	/* Enable interrupt for BMI260 sensor */
	gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);

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

/* Mutexes */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct bmi_drv_data_t g_bmi260_data;
static struct accelgyro_saved_data_t g_bma255_data;

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0,  FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

static const mat33_fp_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMA255,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bma2x2_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bma255_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
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
	 * Note: BMI260: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI260,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi260_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi260_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
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
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI260,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi260_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi260_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif
