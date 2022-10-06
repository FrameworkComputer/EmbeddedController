/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meep/Mimrock board-specific configuration */

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

#define USB_PD_PORT_ANX7447 0
#define USB_PD_PORT_PS8751 1

#ifdef CONFIG_KEYBOARD_KEYPAD
#error "KSO_14 was repurposed to PPC_ID pin so CONFIG_KEYBOARD_KEYPAD \
should not be defined."
#endif

static uint8_t sku_id;
static int c0_port_ppc;
static int c1_port_ppc;

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		if (c0_port_ppc == PPC_SYV682X)
			syv682x_interrupt(0);
		else
			nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		if (c1_port_ppc == PPC_SYV682X)
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
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = { "VBUS_C0", NPCX_ADC_CH9, ADC_MAX_VOLT * 10,
			  ADC_READ_MAX + 1, 0 },
	/* Vbus C1 sensing (10x voltage divider). PPVAR_USB_C1_VBUS */
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
const mat33_fp_t lid_standrd_ref = { { FLOAT_TO_FP(1), 0, 0 },
				     { 0, FLOAT_TO_FP(-1), 0 },
				     { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
				       { 0, FLOAT_TO_FP(-1), 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

/* sensor private data */
static struct kionix_accel_data kx022_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

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
		.drv_data = &kx022_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standrd_ref,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/*
 * Returns 1 for boards that are convertible into tablet mode, and
 * zero for clamshells.
 */
int board_is_convertible(void)
{
	/*
	 * Meep: 1, 2, 3, 4
	 * Vortininja: 49, 50, 51, 52
	 * Unprovisioned: 255
	 */
	return sku_id == 1 || sku_id == 2 || sku_id == 3 || sku_id == 4 ||
	       sku_id == 49 || sku_id == 50 || sku_id == 51 || sku_id == 52 ||
	       sku_id == 255;
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

static int get_ppc_port_config(uint32_t board_version, int port)
{
	switch (port) {
	/*
	 * Meep C0 port PPC was configrated by PPC ID pin only.
	 */
	case USB_PD_PORT_TCPC_0:
		if ((board_version >= 6) && gpio_get_level(GPIO_PPC_ID))
			return PPC_SYV682X;
		else
			return PPC_NX20P348X;
	/*
	 * Meep C1 port PPC was configrated by PPC ID pin or SSFC,
	 * The first of all we should check SSFC with priority one,
	 * then check PPC ID if board is unalbe to get SSFC.
	 */
	case USB_PD_PORT_TCPC_1:
		switch (get_cbi_ssfc_ppc_p1()) {
		case SSFC_PPC_P1_DEFAULT:
			if ((board_version >= 6) && gpio_get_level(GPIO_PPC_ID))
				return PPC_SYV682X;
			else
				return PPC_NX20P348X;
		case SSFC_PPC_P1_SYV682X:
			return PPC_SYV682X;
		case SSFC_PPC_P1_NX20P348X:
		default:
			return PPC_NX20P348X;
		}
	default:
		return PPC_NX20P348X;
	}
}

static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku_id = val;
	ccprints("SKU: 0x%04x", sku_id);

	board_update_sensor_config_from_sku();

	if (cbi_get_board_version(&val) == EC_SUCCESS)
		ccprints("Board Version: %d", val);

	c0_port_ppc = get_ppc_port_config(val, USB_PD_PORT_TCPC_0);
	c1_port_ppc = get_ppc_port_config(val, USB_PD_PORT_TCPC_1);
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

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 }, { 1, 1 },	  { 1, 0 },   { 0, 6 },
	{ 0, 7 },   { 1, 4 }, { 1, 3 },	  { 1, 6 },   { 1, 7 },
	{ 3, 1 },   { 2, 0 }, { 1, 5 },	  { 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 }, { 2, 5 },	  { 1, 2 },   { 2, 3 },
	{ 2, 2 },   { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC, !is_overcurrented);
}

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	/*
	 * We always compile in backlight support for Meep/Dorp, but only some
	 * SKUs come with the hardware. Therefore, check if the current
	 * device is one of them and return the default value - with backlight
	 * here.
	 */
	if (sku_id == 34 || sku_id == 36)
		return flags0;

	/* Report that there is no keyboard backlight */
	return (flags0 &= ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
}

__override uint16_t board_get_ps8xxx_product_id(int port)
{
	/* Meep variant doesn't have ps8xxx product in the port 0 */
	if (port == 0)
		return 0;

	switch (get_cbi_ssfc_tcpc_p1()) {
	case SSFC_TCPC_P1_PS8755:
		return PS8755_PRODUCT_ID;
	case SSFC_TCPC_P1_DEFAULT:
	case SSFC_TCPC_P1_PS8751:
	default:
		return PS8751_PRODUCT_ID;
	}
}

static const struct ppc_config_t ppc_syv682x_port0 = {
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

static const struct ppc_config_t ppc_syv682x_port1 = {
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

static void board_setup_ppc(void)
{
	if (c0_port_ppc == PPC_SYV682X) {
		memcpy(&ppc_chips[USB_PD_PORT_TCPC_0], &ppc_syv682x_port0,
		       sizeof(struct ppc_config_t));

		gpio_set_flags(GPIO_USB_PD_C0_INT_ODL, GPIO_INT_BOTH);
	}

	if (c1_port_ppc == PPC_SYV682X) {
		memcpy(&ppc_chips[USB_PD_PORT_TCPC_1], &ppc_syv682x_port1,
		       sizeof(struct ppc_config_t));

		gpio_set_flags(GPIO_USB_PD_C1_INT_ODL, GPIO_INT_BOTH);
	}
}
DECLARE_HOOK(HOOK_INIT, board_setup_ppc, HOOK_PRIO_INIT_I2C + 2);

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_PD_C0_INT_ODL) == 0;

	return gpio_get_level(GPIO_USB_PD_C1_INT_ODL) == 0;
}
