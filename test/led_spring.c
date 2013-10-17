/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test lid switch.
 */

#include "battery_smart.h"
#include "common.h"
#include "console.h"
#include "driver/led/lp5562.h"
#include "host_command.h"
#include "pmu_tpschrome.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define LP5562_I2C_ADDR (0x30 << 1)
#define LP5562_NUM_WATCH_REG 0x71
static uint8_t lp5562_reg[LP5562_NUM_WATCH_REG];

#define LED_COLOR_NONE   LP5562_COLOR_NONE
#define LED_COLOR_GREEN  LP5562_COLOR_GREEN(0x10)
#define LED_COLOR_YELLOW LP5562_COLOR_BLUE(0x40)
#define LED_COLOR_RED    LP5562_COLOR_RED(0x80)

static enum charging_state mock_charge_state = ST_IDLE;
static int lp5562_failed_i2c_reg = -1;
static const char * const state_names[] = POWER_STATE_NAME_TABLE;

/*****************************************************************************/
/* Mock functions */

static void set_ac(int ac)
{
	gpio_set_level(GPIO_AC_PRESENT, ac);
	ccprintf("[%T TEST AC = %d]\n", ac);
}

enum charging_state charge_get_state(void)
{
	return mock_charge_state;
}

static void set_charge_state(enum charging_state s)
{
	mock_charge_state = s;
	ccprintf("[%T TEST Charge state = %s]\n", state_names[s]);
}

static void set_battery_soc(int soc)
{
	sb_write(SB_RELATIVE_STATE_OF_CHARGE, soc);
	sb_write(SB_ABSOLUTE_STATE_OF_CHARGE, soc);
}

/*****************************************************************************/
/* Test utilities */

static int lp5562_i2c_write8(int port, int slave_addr, int offset, int data)
{
	if (port != I2C_PORT_HOST || slave_addr != LP5562_I2C_ADDR)
		return EC_ERROR_INVAL;
	if (offset == lp5562_failed_i2c_reg)
		return EC_ERROR_UNKNOWN;
	if (offset < LP5562_NUM_WATCH_REG)
		lp5562_reg[offset] = data;
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_WRITE8(lp5562_i2c_write8);

static int lp5562_get_color(void)
{
	return lp5562_reg[LP5562_REG_B_PWM] |
	       (lp5562_reg[LP5562_REG_G_PWM] << 8) |
	       (lp5562_reg[LP5562_REG_R_PWM] << 16);
}

static int lp5562_powered(void)
{
	return lp5562_reg[LP5562_REG_ENABLE] & 0x40;
}

static int lp5562_in_pwm_mode(void)
{
	return lp5562_reg[LP5562_REG_LED_MAP] == 0;
}

static int verify_color(int expected_color)
{
	int actual = lp5562_get_color();

	if (expected_color == LED_COLOR_NONE)
		return !lp5562_powered();
	if (!lp5562_powered())
		return 0;
	if (!lp5562_in_pwm_mode())
		return 0;

	ccprintf("[%T LED color = 0x%06x]\n", actual);

	return actual == expected_color;
}

/*****************************************************************************/
/* Tests */

static int test_led_power(void)
{
	/* Check LED is off */
	TEST_ASSERT(!lp5562_powered());

	/* Plug in AC, and LED should turn on within a second */
	set_ac(1);
	msleep(1500);
	TEST_ASSERT(lp5562_powered());

	/* Change state while AC is on. LED should keep on */
	set_charge_state(ST_CHARGING_ERROR);
	msleep(1500);
	TEST_ASSERT(lp5562_powered());

	/* Unplug AC. LED should turn off */
	set_ac(0);
	msleep(1500);
	TEST_ASSERT(!lp5562_powered());

	/* Plug AC again. LED should turn on */
	set_ac(1);
	msleep(1500);
	TEST_ASSERT(lp5562_powered());

	return EC_SUCCESS;
}

static int test_led_color(void)
{
	/* IDLE0 */
	set_ac(1);
	set_charge_state(ST_IDLE0);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	/* BAD_COND*/
	set_charge_state(ST_BAD_COND);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	/* PRE_CHARGING */
	set_charge_state(ST_PRE_CHARGING);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	/* IDLE */
	set_charge_state(ST_IDLE);
	set_battery_soc(50);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));
	set_battery_soc(99);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* DISCHARGING */
	set_charge_state(ST_DISCHARGING);
	set_battery_soc(50);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));
	set_battery_soc(99);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* CHARGING */
	set_charge_state(ST_CHARGING);
	set_battery_soc(50);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));
	set_battery_soc(99);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* CHARGING_ERROR */
	set_charge_state(ST_CHARGING_ERROR);
	msleep(1500);
	TEST_ASSERT(verify_color(LED_COLOR_RED));

	return EC_SUCCESS;
}

static int test_green_yellow(void)
{
	/* Make LED green */
	set_ac(1);
	set_charge_state(ST_CHARGING);
	set_battery_soc(95);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* Make it yellow now */
	set_battery_soc(90);
	msleep(1500);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	/* Shouldn't change from yellow to green in 15 seconds */
	set_battery_soc(95);
	msleep(13000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	/* After 15 seconds, it should turn green */
	msleep(3000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* Shouldn't change from green to yellow in 15 seconds */
	set_charge_state(ST_BAD_COND);
	msleep(12000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* After 15 seconds, it should turn yellow */
	msleep(4000);
	TEST_ASSERT(verify_color(LED_COLOR_YELLOW));

	return EC_SUCCESS;
}

static int test_bad_i2c(void)
{
	/* Make LED green */
	set_ac(1);
	set_charge_state(ST_DISCHARGING);
	set_battery_soc(95);
	msleep(30000);
	TEST_ASSERT(verify_color(LED_COLOR_GREEN));

	/* Make it red, but fail the I2C write to green PWM register */
	lp5562_failed_i2c_reg = LP5562_REG_G_PWM;
	set_charge_state(ST_CHARGING_ERROR);
	msleep(3000);
	TEST_ASSERT(!verify_color(LED_COLOR_RED));

	/* I2C works again. LED should turn red */
	lp5562_failed_i2c_reg = -1;
	msleep(1500);
	TEST_ASSERT(verify_color(LED_COLOR_RED));

	/* Make it green, but I2C fails again */
	lp5562_failed_i2c_reg = LP5562_REG_R_PWM;
	set_charge_state(ST_DISCHARGING);
	msleep(1500);
	TEST_ASSERT(!verify_color(LED_COLOR_GREEN));
	TEST_ASSERT(!verify_color(LED_COLOR_RED));

	/* I2C works now, but LED turns red at the same time */
	lp5562_failed_i2c_reg = -1;
	set_charge_state(ST_CHARGING_ERROR);
	msleep(1500);
	TEST_ASSERT(verify_color(LED_COLOR_RED));

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_led_power);
	RUN_TEST(test_led_color);
	RUN_TEST(test_green_yellow);
	RUN_TEST(test_bad_i2c);

	test_print_result();
}
