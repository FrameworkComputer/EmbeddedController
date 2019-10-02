/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bobba board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/sync.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpci.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define USB_PD_PORT_ANX7447	0
#define USB_PD_PORT_PS8751	1

static uint8_t sku_id;

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {-1, -1}, {-1, -1}, {1, 4}, {1, 3},
		{-1, -1}, {1, 6}, {1, 7}, {3, 1}, {2, 0},
		{1, 5}, {2, 6}, {2, 7}, {2, 1}, {2, 4},
		{2, 5}, {1, 2}, {2, 3}, {2, 2}, {3, 0},
		{-1, -1}, {0, 4}, {-1, -1}, {8, 2}, {-1, -1},
		{-1, -1},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		nx20p348x_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and GPIO interrupts*/
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_AMB] = {
		"TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	[ADC_TEMP_SENSOR_CHARGER] = {
		"TEMP_CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	/* Vbus sensing (1/10 voltage divider). */
	[ADC_VBUS_C0] = {
		"VBUS_C0", NPCX_ADC_CH9, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	[ADC_VBUS_C1] = {
		"VBUS_C1", NPCX_ADC_CH4, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = {.name = "Battery",
				 .type = TEMP_SENSOR_TYPE_BATTERY,
				 .read = charge_get_battery_temp,
				 .idx = 0,
				 .action_delay_sec = 1},
	[TEMP_SENSOR_AMBIENT] = {.name = "Ambient",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_51k1_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_AMB,
				 .action_delay_sec = 5},
	[TEMP_SENSOR_CHARGER] = {.name = "Charger",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_13k7_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_CHARGER,
				 .action_delay_sec = 1},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, 0,  FLOAT_TO_FP(1)}
};

/*
 * Sparky360 SKU ID 26 has AR Cam, and move base accel/gryo to AR Cam board.
 * AR Cam board has about 16° bias with motherboard through Y axis.
 * Rotation matrix with 16° through Y axis:
 *     | cos(16°)      0       sin(16°)|   | 0.96126   0   0.27564|
 * R = |    0          1          0    | = |     0     1      0   |
 *     |-sin(16°)      0       cos(16°)|   |-0.27564   0   0.96126|
 *
 *                                           |0  -0.96126   0.27564|
 * base_ar_cam_ref = R * base_standard_ref = |1       0        0   |
 *                                           |0   0.27564   0.96126|
 */
const mat33_fp_t base_ar_cam_ref = {
	{ 0, FLOAT_TO_FP(-0.96126), FLOAT_TO_FP(0.27564)},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(0.27564), FLOAT_TO_FP(0.96126)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi160_drv_data_t g_bmi160_data;

/* Drivers */
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
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 4, /* g */
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
	 .default_range = 4,  /* g */
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
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
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	},
	[VSYNC] = {
	.name = "Camera VSYNC",
	.active_mask = SENSOR_ACTIVE_S0,
	.chip = MOTIONSENSE_CHIP_GPIO,
	.type = MOTIONSENSE_TYPE_SYNC,
	.location = MOTIONSENSE_LOC_CAMERA,
	.drv = &sync_drv,
	.default_range = 0,
	.min_frequency = 0,
	.max_frequency = 1,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int board_is_convertible(void)
{
	/* SKU ID of Bobba360, Sparky360, & unprovisioned: 9, 10, 11, 12, 25, 26, 255 */
	return sku_id == 9 || sku_id == 10 || sku_id == 11 || sku_id == 12
		|| sku_id == 25 || sku_id == 26 || sku_id == 255;
}

static int board_with_ar_cam(void)
{
	/* SKU ID of Sparky360 with AR Cam: 26 */
	return sku_id == 26;
}

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_convertible()) {
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable Base Accel interrupt */
		gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}

	/* Sparky360 with AR Cam: base accel/gyro sensor is on AR Cam board. */
	if (board_with_ar_cam()) {
		/* Enable interrupt from camera */
		gpio_enable_interrupt(GPIO_WFCAM_VSYNC);

		motion_sensors[BASE_ACCEL].rot_standard_ref = &base_ar_cam_ref;
		motion_sensors[BASE_GYRO].rot_standard_ref = &base_ar_cam_ref;
	} else {
		/* Camera isn't stuffed, don't allow line to float */
		gpio_set_flags(GPIO_WFCAM_VSYNC, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static int board_has_keypad(void)
{
	return sku_id == 41 || sku_id == 42 || sku_id == 43 || sku_id == 44;
}

static void board_update_no_keypad_config_from_sku(void)
{
	if (!board_has_keypad()) {
#ifndef TEST_BUILD
		/* Disable scanning KSO13 & 14 if keypad isn't present. */
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
		keyscan_config.actual_key_mask[11] = 0xfa;
		keyscan_config.actual_key_mask[12] = 0xca;

		/* Search key is moved back to col=1,row=0 */
		keyscan_config.actual_key_mask[0] = 0x14;
		keyscan_config.actual_key_mask[1] = 0xff;
#endif
	}
}

static void board_usb_charge_mode_init(void)
{
	int i;

	/*
	 * Only overriding the USB_DISALLOW_SUSPEND_CHARGE in RO is enough because
	 * USB_SYSJUMP_TAG preserves the settings to RW. And we should honor to it.
	 */
	if (system_jumped_to_this_image())
		return;

	/* Currently only blorb and droid support this feature. */
	if ((sku_id < 32 || sku_id > 39) && (sku_id < 40 || sku_id > 47))
		return;

	/*
	 * By default, turn the charging off when system suspends.
	 * If system power on with connecting a USB device,
	 * the OS must send an event to EC to clear the
	 * inhibit_charging_in_suspend.
	 */
	for (i = 0; i < CONFIG_USB_PORT_POWER_SMART_PORT_COUNT; i++)
		usb_charge_set_mode(i, CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE,
				USB_DISALLOW_SUSPEND_CHARGE);
}
/*
 * usb_charge_init() is hooked in HOOK_PRIO_DEFAULT and set inhibit_charge to
 * USB_ALLOW_SUSPEND_CHARGE. As a result, in order to override this default
 * setting to USB_DISALLOW_SUSPEND_CHARGE this function should be hooked after
 * calling usb_charge_init().
 */
DECLARE_HOOK(HOOK_INIT, board_usb_charge_mode_init, HOOK_PRIO_DEFAULT + 1);

/* Read CBI from i2c eeprom and initialize variables for board variants */
static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) != EC_SUCCESS || val > UINT8_MAX)
		return;
	sku_id = val;
	CPRINTSUSB("SKU: %d", sku_id);

	board_update_sensor_config_from_sku();
	board_update_no_keypad_config_from_sku();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

uint32_t board_override_feature_flags0(uint32_t flags0)
{
	/*
	 * Remove keyboard backlight feature for devices that don't support it.
	 */
	if (sku_id == 33 || sku_id == 34 || sku_id == 41 || sku_id == 42)
		return flags0;
	else
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
}

uint32_t board_override_feature_flags1(uint32_t flags1)
{
	return flags1;
}

void board_hibernate_late(void) {

	int i;

	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs before going to hibernate */
		{GPIO_BAT_LED_BLUE_L, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_ORANGE_L, GPIO_INPUT | GPIO_PULL_UP},
	};

	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);
}

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;
	if (board_is_convertible())
		keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Sanity check the port. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC, !is_overcurrented);
}
