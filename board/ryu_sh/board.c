/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu sensor hub configuration */

#include "common.h"
#include "console.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "motion_sense.h"
#include "power.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Sensor mutex */
static struct mutex g_mutex;

struct motion_sensor_t motion_sensors[] = {

	/*
	 * Note: lsm6ds0: supports accelerometer and gyro sensor
	 * Requriement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	{SENSOR_ACTIVE_S0_S3, "Accel", MOTIONSENSE_CHIP_LSM6DS0,
		MOTIONSENSE_TYPE_ACCEL, MOTIONSENSE_LOC_LID,
		&lsm6ds0_drv, &g_mutex, NULL,
		LSM6DS0_ADDR1, NULL, 119000, 2},

	{SENSOR_ACTIVE_S0_S3, "Gyro", MOTIONSENSE_CHIP_LSM6DS0,
		MOTIONSENSE_TYPE_GYRO, MOTIONSENSE_LOC_LID,
		&lsm6ds0_drv, &g_mutex, NULL,
		LSM6DS0_ADDR1, NULL, 119000, 2000},

};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void board_config_pre_init(void)
{
	/*
	 *  enable SYSCFG clock:
	 *  otherwise the SYSCFG peripheral is not clocked during the pre-init
	 *  and the register write as no effect.
	 */
	STM32_RCC_APB2ENR |= 1 << 0;
	/*
	 * Remap USART DMA to match the USART driver
	 * the DMA mapping is :
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);/* Remap USART1 RX/TX DMA */
}
