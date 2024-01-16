/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Treeya board-specific configuration */

#include "button.h"
#include "common.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "switch.h"
#include "system.h"
#include "system_chip.h"
#include "tablet_mode.h"
#include "task.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

static uint8_t is_psl_hibernate;

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 100,
	  .scl = GPIO_I2C0_SCL,
	  .sda = GPIO_I2C0_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "thermal",
	  .port = I2C_PORT_THERMAL_AP,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
	{ .name = "sensor",
	  .port = I2C_PORT_SENSOR,
	  .kbps = 400,
	  .scl = GPIO_I2C7_SCL,
	  .sda = GPIO_I2C7_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Motion sensors */
static struct mutex g_lid_mutex_1;
static struct mutex g_base_mutex_1;

/* Lid accel private data */
static struct stprivate_data g_lis2dwl_data;
/* Base accel private data */
static struct lsm6dsm_data g_lsm6dsm_data = LSM6DSM_DATA;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t lsm6dsm_base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
						      { 0, FLOAT_TO_FP(-1), 0 },
						      { 0, 0,
							FLOAT_TO_FP(1) } };

static const mat33_fp_t treeya_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
						{ FLOAT_TO_FP(1), 0, 0 },
						{ 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t lid_accel_1 = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LIS2DWL,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &lis2dw12_drv,
	.mutex = &g_lid_mutex_1,
	.drv_data = &g_lis2dwl_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	.rot_standard_ref = NULL,
	.default_range = 2, /* g */
	.min_frequency = LIS2DW12_ODR_MIN_VAL,
	.max_frequency = LIS2DW12_ODR_MAX_VAL,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 12500 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t base_accel_1 = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex_1,
	.drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.rot_standard_ref = &lsm6dsm_base_standard_ref,
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
};

struct motion_sensor_t base_gyro_1 = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex_1,
	.drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data, MOTIONSENSE_TYPE_GYRO),
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.default_range = 1000 | ROUND_UP_FLAG, /* dps */
	.rot_standard_ref = &lsm6dsm_base_standard_ref,
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
};

static int board_use_st_sensor(void)
{
	/* sku_id 0xa8-0xa9, 0xbe, 0xbf use ST sensors */
	uint32_t sku_id = system_get_sku_id();

	if (sku_id == 0xa8 || sku_id == 0xa9 || sku_id == 0xbe ||
	    sku_id == 0xbf)
		return 1;
	else
		return 0;
}

/* treeya board will use two sets of lid/base sensor, we need update
 * sensors info according to sku id.
 */
void board_update_sensor_config_from_sku(void)
{
	uint32_t sku_id = system_get_sku_id();

	if (board_is_convertible()) {
		/* sku_id a8-a9 use ST sensors */
		if (board_use_st_sensor()) {
			motion_sensors[LID_ACCEL] = lid_accel_1;
			motion_sensors[BASE_ACCEL] = base_accel_1;
			motion_sensors[BASE_GYRO] = base_gyro_1;
		} else {
			/*Need to change matrix for treeya*/
			motion_sensors[BASE_ACCEL].rot_standard_ref =
				&treeya_standard_ref;
			motion_sensors[BASE_GYRO].rot_standard_ref =
				&treeya_standard_ref;
		}

		/* Enable Gyro interrupts */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_set_mode(0, TABLET_TRIGGER_LID);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}

	if (sku_id == 160 || sku_id == 168 || sku_id == 169 || sku_id == 190 ||
	    sku_id == 191) {
		is_psl_hibernate = 0;
	} else {
		is_psl_hibernate = 1;
	}
}

/* bmi160 or lsm6dsm need differenct interrupt function */
void board_bmi160_lsm6dsm_interrupt(enum gpio_signal signal)
{
	if (board_use_st_sensor())
		lsm6dsm_interrupt(signal);
	else
		bmi160_interrupt(signal);
}

static void system_psl_type_sel(int psl_no, uint32_t flags)
{
	/* Set PSL input events' type as level or edge trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW))
		CLEAR_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);
	else if ((flags & GPIO_INT_F_RISING) || (flags & GPIO_INT_F_FALLING))
		SET_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);

	/*
	 * Set PSL input events' polarity is low (high-to-low) active or
	 * high (low-to-high) active
	 */
	if (flags & GPIO_HIB_WAKE_HIGH)
		SET_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
	else
		CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
}

int system_config_psl_mode(enum gpio_signal signal)
{
	int psl_no;
	const struct gpio_info *g = gpio_list + signal;

	if (g->port == GPIO_PORT_D && g->mask == MASK_PIN2) /* GPIOD2 */
		psl_no = 0;
	else if (g->port == GPIO_PORT_0 && (g->mask & 0x07)) /* GPIO00/01/02 */
		psl_no = GPIO_MASK_TO_NUM(g->mask) + 1;
	else
		return 0;

	system_psl_type_sel(psl_no, g->flags);
	return 1;
}

void system_enter_psl_mode(void)
{
	/* Configure pins from GPIOs to PSL which rely on VSBY power rail. */
	gpio_config_module(MODULE_PMU, 1);

	/*
	 * Only PSL_IN events can pull PSL_OUT to high and reboot ec.
	 * We should treat it as wake-up pin reset.
	 */
	NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PIN;

	/*
	 * Pull PSL_OUT (GPIO85) to low to cut off ec's VCC power rail by
	 * setting bit 5 of PDOUT(8).
	 */
	SET_BIT(NPCX_PDOUT(GPIO_PORT_8), 5);
}

/* Hibernate function implemented by PSL (Power Switch Logic) mode. */
__noreturn void __keep __enter_hibernate_in_psl(void)
{
	system_enter_psl_mode();
	/* Spin and wait for PSL cuts power; should never return */
	while (1)
		;
}

void board_hibernate_late(void)
{
	int i;

	/*
	 * If the SKU cannot use PSL hibernate, immediately return to go the
	 * non-PSL hibernate flow.
	 */
	if (!is_psl_hibernate) {
		NPCX_KBSINPU = 0x0A;
		return;
	}

	for (i = 0; i < hibernate_wake_pins_used; i++) {
		/* Config PSL pins setting for wake-up inputs */
		if (!system_config_psl_mode(hibernate_wake_pins[i]))
			ccprintf("Invalid PSL setting in wake-up pin %d\n", i);
	}

	/* Clear all pending IRQ otherwise wfi will have no affect */
	for (i = NPCX_IRQ_0; i < NPCX_IRQ_COUNT; i++)
		task_clear_pending_irq(i);

	__enter_hibernate_in_psl();
}
