/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "button.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/ioexpander/pcal6408.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "usb_charge.h"

/* This I2C moved. Temporarily detect and support the V0 HW. */
int I2C_PORT_BATTERY = I2C_PORT_BATTERY_V1;

/* Interrupt handler varies with DB option. */
void (*c1_tcpc_config_interrupt)(enum gpio_signal signal) = tcpc_alert_event;

void c1_tcpc_interrupt(enum gpio_signal signal)
{
	c1_tcpc_config_interrupt(signal);
}

#include "gpio_list.h"

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct stprivate_data g_lis2dwl_data;
static struct lsm6dsm_data g_lsm6dsm_data = LSM6DSM_DATA;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LIS2DWL,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lis2dw12_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_lis2dwl_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 2, /* g, enough for laptop. */
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
	},

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	 .int_signal = GPIO_6AXIS_INT_L,
	 .flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 4, /* g, enough for laptop */
	 .rot_standard_ref = &base_standard_ref,
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
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_GYRO),
	.int_signal = GPIO_6AXIS_INT_L,
	.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 1000 | ROUND_UP_FLAG, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#endif /* HAS_TASK_MOTIONSENSE */

/* These IO expander GPIOs vary with DB option. */
enum gpio_signal IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT1;
enum gpio_signal IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT1;

static void pcal6408_handler(void)
{
	pcal6408_ioex_event_handler(IOEX_HDMI_PCAL6408);
}
DECLARE_DEFERRED(pcal6408_handler);

void pcal6408_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pcal6408_handler_data, 0);
}

static void setup_fw_config(void)
{
	if (ec_config_get_usb_db() == DALBOZ_DB_D_OPT2_USBA_HDMI) {
		ccprints("DB OPT2 HDMI");
		ioex_config[IOEX_HDMI_PCAL6408].flags = 0;
		ioex_init(IOEX_HDMI_PCAL6408);
		IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT2;
		IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT2;
		usb_port_enable[USBA_PORT_A1] = IOEX_EN_USB_A1_5V_DB_OPT2;
		c1_tcpc_config_interrupt = pcal6408_interrupt;
		ioex_enable_interrupt(IOEX_HDMI_CONN_HPD_3V3_DB);
	} else {
		ccprints("DB OPT1 USBC");
		ioex_config[IOEX_C1_NCT3807].flags = 0;
		ioex_init(IOEX_C1_NCT3807);
		IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT1;
		IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT1;
		usb_port_enable[USBA_PORT_A1] = IOEX_EN_USB_A1_5V_DB_OPT1;
		c1_tcpc_config_interrupt = tcpc_alert_event;
	}

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_FAULT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC 1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);

	if (ec_config_has_lid_angle_tablet_mode()) {
		/* Enable Gyro interrupts */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_set_mode(0);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/*
 * If the battery is found on the V0 I2C port then re-map the battery port.
 * Use HOOK_PRIO_INIT_I2C so we re-map before init_battery_type() and
 * charger_chips_init() want to talk to the battery.
 */
static void check_v0_battery(void)
{
	int status;

	if (i2c_read16(I2C_PORT_BATTERY_V0, BATTERY_ADDR_FLAGS,
			SB_BATTERY_STATUS, &status) == EC_SUCCESS) {
		ccprints("V0 HW detected");
		I2C_PORT_BATTERY = I2C_PORT_BATTERY_V0;
	}
}
DECLARE_HOOK(HOOK_INIT, check_v0_battery, HOOK_PRIO_INIT_I2C);
