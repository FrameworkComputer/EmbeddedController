/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP board-specific configuration */

#include "button.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "pca9555.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h"

#define I2C_PORT_PCA555_PMIC_GPIO	NPCX_I2C_PORT0_0
#define I2C_ADDR_PCA555_PMIC_GPIO_FLAGS	0x21
#define PCA555_PMIC_GPIO_WRITE(reg, data) \
		pca9555_write(I2C_PORT_PCA555_PMIC_GPIO, \
			I2C_ADDR_PCA555_PMIC_GPIO_FLAGS, (reg), (data))
#define PCA555_PMIC_GPIO_READ(reg, data) \
		pca9555_read(I2C_PORT_PCA555_PMIC_GPIO, \
			I2C_ADDR_PCA555_PMIC_GPIO_FLAGS, (reg), (data))

#define I2C_PORT_PCA555_BOARD_ID_GPIO	NPCX_I2C_PORT0_0
#define I2C_ADDR_PCA555_BOARD_ID_GPIO_FLAGS	0x20
#define PCA555_BOARD_ID_GPIO_READ(reg, data) \
		pca9555_read(I2C_PORT_PCA555_BOARD_ID_GPIO, \
			I2C_ADDR_PCA555_BOARD_ID_GPIO_FLAGS, (reg), (data))

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"pmic",      NPCX_I2C_PORT0_0, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"typec",     NPCX_I2C_PORT7_0, 400, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
	{"master1",   NPCX_I2C_PORT1_0, 400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"master2",   NPCX_I2C_PORT2_0, 100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"charger",   NPCX_I2C_PORT3_0, 100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
	int data;

	if (PCA555_PMIC_GPIO_READ(PCA9555_CMD_OUTPUT_PORT_0, &data))
		return;

	/*
	 * No need to re-init PMIC since settings are sticky across sysjump.
	 * However, be sure to check that PMIC is already enabled. If it is
	 * then there's no need to re-sequence the PMIC.
	 */
	if (system_jumped_to_this_image() && (data & PCA9555_IO_0))
		return;

	/* Enable SOC_3P3_EN_L: Set the Output port O0.1 to low level */
	data &= ~PCA9555_IO_1;
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_OUTPUT_PORT_0, data);

	/* TODO: Find out from the spec */
	msleep(10);

	/* Enable PMIC_EN: Set the Output port O0.0 to high level */
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_OUTPUT_PORT_0, data | PCA9555_IO_0);
}

/* Initialize board. */
static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_FIRST);

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

void chipset_do_shutdown(void)
{
	int data;

	if (PCA555_PMIC_GPIO_READ(PCA9555_CMD_OUTPUT_PORT_0, &data))
		return;

	/* Disable SOC_3P3_EN_L: Set the Output port O0.1 to high level */
	data |= PCA9555_IO_1;
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_OUTPUT_PORT_0, data);

	/* TODO: Find out from the spec */
	msleep(10);

	/* Disable PMIC_EN: Set the Output port O0.0 to low level */
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_OUTPUT_PORT_0, data & ~PCA9555_IO_0);
}

void board_hibernate_late(void)
{
}

void board_hibernate(void)
{
	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_do_shutdown();

	/* Added delay to allow AP to settle down */
	msleep(100);
}

int board_get_version(void)
{
	int data;

	if (PCA555_BOARD_ID_GPIO_READ(PCA9555_CMD_INPUT_PORT_1, &data))
		return -1;

	return data & 0x0f;
}

static void pmic_init(void)
{
	/* No need to re-init PMIC since settings are sticky across sysjump. */
	if (system_jumped_late())
		return;

	/*
	 * PMIC INIT
	 * Configure Port O0.0 as Output port - PMIC_EN
	 * Configure Port O0.1 as Output port - SOC_3P3_EN_L
	 */
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_CONFIGURATION_PORT_0, 0xfc);

	/*
	 * Set the Output port O0.0 to low level - PMIC_EN
	 * Set the Output port O0.1 to high level - SOC_3P3_EN_L
	 *
	 * POR of PCA9555 port is input with high impedance hence explicitly
	 * configure the SOC_3P3_EN_L to high level.
	 */
	PCA555_PMIC_GPIO_WRITE(PCA9555_CMD_OUTPUT_PORT_0, 0xfe);
}
DECLARE_HOOK(HOOK_INIT, pmic_init, HOOK_PRIO_INIT_I2C + 1);
