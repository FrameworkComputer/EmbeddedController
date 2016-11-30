/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP board-specific configuration */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
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

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,     1, "RSMRST_L"},
	{GPIO_PCH_SLP_S3_L,       1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,       1, "SLP_S4_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,      1, "ALL_SYS_PGOOD"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master0-0", NPCX_I2C_PORT0_0, 400, GPIO_I2C0_SCL0, GPIO_I2C0_SDA0},
	{"master0-1", NPCX_I2C_PORT0_1, 400, GPIO_I2C0_SCL1, GPIO_I2C0_SDA1},
	{"master1",   NPCX_I2C_PORT1,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"master2",   NPCX_I2C_PORT2,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"master3",   NPCX_I2C_PORT3,   100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* Called by APL power state machine when transitioning from G3 to S5 */
static void chipset_pre_init(void)
{
	/*
	 * No need to re-init PMIC since settings are sticky across sysjump.
	 * However, be sure to check that PMIC is already enabled. If it is
	 * then there's no need to re-sequence the PMIC.
	 */
	if (system_jumped_to_this_image())
		return;

	/* TODO: Enable PMIC */
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);


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
	/* TODO: Disable PMIC */
	/* gpio_set_level(GPIO_PMIC_EN, 0); */
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

int charge_prevent_power_on(int power_button_pressed)
{
	return 0;
}

int extpower_is_present(void)
{
	return 1;
}

int charge_want_shutdown(void)
{
	return 0;
}
