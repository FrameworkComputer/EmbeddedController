/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ampton/Apel board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/ppc/sn5s330.h"
#include "driver/sync.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/usb_mux/it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpci.h"
#include "temp_sensor.h"
#include "thermistor.h"
#include "uart.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

static uint8_t sku_id;

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_PD_INT_ODL)
		sn5s330_interrupt(0);
	else if (signal == GPIO_USB_C1_PD_INT_ODL)
		sn5s330_interrupt(1);
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PD_INT_ODL) == 0;
	else
		return gpio_get_level(GPIO_USB_C1_PD_INT_ODL) == 0;
}

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* USB-C MUX Configuration */

#define USB_PD_PORT_ITE_0      0
#define USB_PD_PORT_ITE_1      1

static int tune_mux(int port);

struct usb_mux ampton_usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		/* Use PS8751 as mux only */
		.port_addr = MUX_PORT_AND_ADDR(
			I2C_PORT_USBC0, PS8751_I2C_ADDR1_FLAGS),
		.flags = USB_MUX_FLAG_NOT_TCPC,
		.driver = &ps8xxx_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		.board_init = &tune_mux,
	},
	[USB_PD_PORT_ITE_1] = {
		/* Use PS8751 as mux only */
		.port_addr = MUX_PORT_AND_ADDR(
			I2C_PORT_USBC1, PS8751_I2C_ADDR1_FLAGS),
		.flags = USB_MUX_FLAG_NOT_TCPC,
		.driver = &ps8xxx_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		.board_init = &tune_mux,
	}
};

/* Some external monitors can't display content normally (eg. ViewSonic VX2880).
 * We need to turn the mux for monitors to function normally.
 */
static int tune_mux(int port)
{
	/* Auto EQ disabled, compensate for channel lost up to 3.6dB */
	mux_write(port, PS8XXX_REG_MUX_DP_EQ_CONFIGURATION, 0x98);
	/* DP output swing adjustment +15% */
	mux_write(port, PS8XXX_REG_MUX_DP_OUTPUT_CONFIGURATION, 0xc0);

	return EC_SUCCESS;
}
/******************************************************************************/
/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {.name = "VBUS_C0",
			 .factor_mul = 10 * ADC_MAX_MVOLT,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH13},
	/* Vbus C1 sensing (10x voltage divider). SUB_EC_ADC */
	[ADC_VBUS_C1] = {.name = "VBUS_C1",
			 .factor_mul = 10 * ADC_MAX_MVOLT,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH14},
	/* Convert to raw mV for thermistor table lookup */
	[ADC_TEMP_SENSOR_AMB] = {.name = "TEMP_AMB",
			 .factor_mul = ADC_MAX_MVOLT,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH3},
	/* Convert to raw mV for thermistor table lookup */
	[ADC_TEMP_SENSOR_CHARGER] = {.name = "TEMP_CHARGER",
			 .factor_mul = ADC_MAX_MVOLT,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH5},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = {.name = "Battery",
				 .type = TEMP_SENSOR_TYPE_BATTERY,
				 .read = charge_get_battery_temp,
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

const mat33_fp_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

const mat33_fp_t gyro_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
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
	 .rot_standard_ref = &lid_standard_ref,
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
	 .rot_standard_ref = &gyro_standard_ref,
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
	/* SKU IDs of Ampton & unprovisioned: 1, 2, 3, 4, 255 */
	return sku_id == 1 || sku_id == 2 || sku_id == 3 || sku_id == 4
		|| sku_id == 255;
}

static int board_with_ar_cam(void)
{
	/* SKU ID of Ampton with AR Cam: 3, 4 */
	return sku_id == 3 || sku_id == 4;
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

	if (board_with_ar_cam()) {
		/* Enable interrupt from camera */
		gpio_enable_interrupt(GPIO_WFCAM_VSYNC);
	} else {
		/* Camera isn't stuffed, don't allow line to float */
		gpio_set_flags(GPIO_WFCAM_VSYNC, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static void board_customize_usbc_mux(uint32_t board_version)
{
	if (board_version > 0) {
		/* not proto, override the mux setting */
		memcpy(usb_muxes, ampton_usb_muxes, sizeof(ampton_usb_muxes));
	}
}

/* Read CBI from i2c eeprom and initialize variables for board variants */
static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) != EC_SUCCESS)
		return;
	sku_id = val;
	ccprints("SKU: %d", sku_id);

	board_update_sensor_config_from_sku();

	if (cbi_get_board_version(&val) != EC_SUCCESS)
		return;
	ccprints("Board version: %d", val);
	board_customize_usbc_mux(val);
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

void board_hibernate_late(void)
{
	/*
	 * Set KSO/KSI pins to GPIO input function to disable keyboard scan
	 * while hibernating. This also prevent leakage current caused
	 * by internal pullup of keyboard scan module.
	 */
	gpio_set_flags_by_mask(GPIO_KSO_H, 0xff, GPIO_INPUT);
	gpio_set_flags_by_mask(GPIO_KSO_L, 0xff, GPIO_INPUT);
	gpio_set_flags_by_mask(GPIO_KSI, 0xff, GPIO_INPUT);
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO(b/78344554): pass this signal upstream once hardware reworked */
	cprints(CC_USBPD, "p%d: overcurrent!", port);
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
