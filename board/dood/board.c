/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* dood board-specific configuration */

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/syv682x.h"
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
#include "stdbool.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define USB_PD_PORT_ANX7447 0
#define USB_PD_PORT_PS8751 1

static uint8_t sku_id;

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

/* Check PPC ID and board version to decide which one ppc is used. */
static bool support_syv_ppc(void)
{
	uint32_t board_version = 0;

	if (cbi_get_board_version(&board_version) != EC_SUCCESS)
		CPRINTSUSB("Get board version failed.");

	if ((board_version >= 5) && (gpio_get_level(GPIO_PPC_ID)))
		return true;

	return false;
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		if (support_syv_ppc())
			syv682x_interrupt(0);
		else
			nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		if (support_syv_ppc())
			syv682x_interrupt(1);
		else
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
	[ADC_TEMP_SENSOR_AMB] = { "TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT,
				  ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_CHARGER] = { "TEMP_CHARGER", NPCX_ADC_CH1,
				      ADC_MAX_VOLT, ADC_READ_MAX + 1, 0 },
	/* Vbus sensing (1/10 voltage divider). */
	[ADC_VBUS_C0] = { "VBUS_C0", NPCX_ADC_CH9, ADC_MAX_VOLT * 10,
			  ADC_READ_MAX + 1, 0 },
	[ADC_VBUS_C1] = { "VBUS_C1", NPCX_ADC_CH4, ADC_MAX_VOLT * 10,
			  ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = { .name = "Battery",
				  .type = TEMP_SENSOR_TYPE_BATTERY,
				  .read = charge_get_battery_temp,
				  .idx = 0 },
	[TEMP_SENSOR_AMBIENT] = { .name = "Ambient",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_51k1_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_AMB },
	[TEMP_SENSOR_CHARGER] = { .name = "Charger",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_13k7_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
				       { FLOAT_TO_FP(1), 0, 0 },
				       { 0, 0, FLOAT_TO_FP(1) } };

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

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
	 .default_range = 2, /* g */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
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
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
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
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int board_is_convertible(void)
{
	/* Default supports convertible */
	return true;
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
}

/* Read CBI from i2c eeprom and initialize variables for board variants */
static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) != EC_SUCCESS || val > UINT8_MAX)
		return;
	sku_id = val;
	CPRINTSUSB("SKU: %d", sku_id);

	board_update_sensor_config_from_sku();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

void board_hibernate_late(void)
{
	int i;

	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs before going to hibernate */
		{ GPIO_BAT_LED_WHITE_L, GPIO_INPUT | GPIO_PULL_UP },
		{ GPIO_BAT_LED_AMBER_L, GPIO_INPUT | GPIO_PULL_UP },
	};

	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);
}

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
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

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC, !is_overcurrented);
}

const struct ppc_config_t ppc_syv682x_port0 = {
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

const struct ppc_config_t ppc_syv682x_port1 = {
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

static void board_setup_ppc(void)
{
	if (!support_syv_ppc())
		return;

	memcpy(&ppc_chips[USB_PD_PORT_TCPC_0], &ppc_syv682x_port0,
	       sizeof(struct ppc_config_t));
	memcpy(&ppc_chips[USB_PD_PORT_TCPC_1], &ppc_syv682x_port1,
	       sizeof(struct ppc_config_t));

	gpio_set_flags(GPIO_USB_PD_C0_INT_ODL, GPIO_INT_BOTH);
	gpio_set_flags(GPIO_USB_PD_C1_INT_ODL, GPIO_INT_BOTH);
}
DECLARE_HOOK(HOOK_INIT, board_setup_ppc, HOOK_PRIO_INIT_I2C + 2);

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_PD_C0_INT_ODL) == 0;

	return gpio_get_level(GPIO_USB_PD_C1_INT_ODL) == 0;
}
