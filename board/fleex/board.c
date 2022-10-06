/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fleex board-specific configuration */

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_lis2dh.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/charger/isl923x.h"
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
#include "task.h"
#include "tablet_mode.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define USB_PD_PORT_ANX7447 0
#define USB_PD_PORT_PS8751 1

static uint8_t sku_id;
static int is_support_syv_ppc;

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

/* Check PPC_ID pin status to decide which one ppc is used. */
static int board_is_syv_ppc(void)
{
	return gpio_get_level(GPIO_PPC_ID);
}

static void board_update_ppc_config_from_board(void)
{
	if (!is_support_syv_ppc)
		return;

	memcpy(&ppc_chips[USB_PD_PORT_TCPC_0], &ppc_syv682x_port0,
	       sizeof(struct ppc_config_t));
	memcpy(&ppc_chips[USB_PD_PORT_TCPC_1], &ppc_syv682x_port1,
	       sizeof(struct ppc_config_t));

	gpio_set_flags(GPIO_USB_PD_C0_INT_ODL, GPIO_INT_BOTH);
	gpio_set_flags(GPIO_USB_PD_C1_INT_ODL, GPIO_INT_BOTH);
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		if (is_support_syv_ppc)
			syv682x_interrupt(0);
		else
			nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		if (is_support_syv_ppc)
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

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
				      { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(-1), 0 },
				       { 0, 0, FLOAT_TO_FP(1) } };

/* sensor private data */
static struct stprivate_data g_lis2dh_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DE,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dh_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2dh_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DH_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2, /* g */
		/* We only use 2g because its resolution is only 8-bits */
		.min_frequency = LIS2DH_ODR_MIN_VAL,
		.max_frequency = LIS2DH_ODR_MAX_VAL,
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

static int board_is_convertible(void)
{
	return sku_id == 0x21 || sku_id == 0x22 || sku_id == 0x23 ||
	       sku_id == 0xff;
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

static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku_id = val;
	ccprints("SKU: 0x%04x", sku_id);

	board_update_sensor_config_from_sku();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

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

static void charger_set_buck_boost_mode(void)
{
	int reg;
	/* Reduce Buck-boost mode switching frequency to improve power
	 * efficiency. */
	if (i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
		       ISL9238_REG_CONTROL3, &reg) == EC_SUCCESS) {
		reg |= ISL9238_C3_BB_SWITCHING_PERIOD;
		if (i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				ISL9238_REG_CONTROL3, reg))
			ccprints("Failed to set isl9238");
	}
}

static void board_init(void)
{
	charger_set_buck_boost_mode();

	is_support_syv_ppc = board_is_syv_ppc();

	board_update_ppc_config_from_board();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_PD_C0_INT_ODL) == 0;

	return gpio_get_level(GPIO_USB_PD_C1_INT_ODL) == 0;
}
