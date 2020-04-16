/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* nucleo-f411re development board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi_common.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "motion_sense.h"

#include "gpio.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void user_button_evt(enum gpio_signal signal)
{
	ccprintf("Button %d, %d!\n", signal, gpio_get_level(signal));
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON_L);

	/* No power control yet */
	/* Go to S3 state */
	hook_notify(HOOK_CHIPSET_STARTUP);

	/* Go to S0 state */
	hook_notify(HOOK_CHIPSET_RESUME);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Arduino connectors analog pins */
	[ADC1_0] = {"ADC1_0",  3000, 4096, 0, STM32_AIN(0)},
	[ADC1_1] = {"ADC1_1",  3000, 4096, 0, STM32_AIN(1)},
	[ADC1_4] = {"ADC1_4",  3000, 4096, 0, STM32_AIN(4)},
	[ADC1_8] = {"ADC1_8",  3000, 4096, 0, STM32_AIN(8)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
	 GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Base Sensor mutex */
static struct mutex g_base_mutex;

static struct bmi_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for lid angle detection */
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
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#ifdef CONFIG_DMA_HELP
#include "dma.h"
int command_dma_help(int argc, char **argv)
{
	dma_dump(STM32_DMA2_STREAM0);
	dma_test(STM32_DMA2_STREAM0);
	dma_dump(STM32_DMA2_STREAM0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dmahelp, command_dma_help,
			NULL, "Run DMA test");
#endif
