/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <ztest.h>

#include "common.h"
#include "i2c/i2c.h"
#include "i2c.h"

/* Unused: required for shimming i2c. */
void watchdog_reload(void)
{
}

static void test_i2c_port_count(void)
{
	zassert_equal(I2C_PORT_COUNT, 2,
		      "I2C_PORT_COUNT expected to be 2 but was %d",
		      I2C_PORT_COUNT);
}

static void test_i2c_lock(void)
{
	i2c_lock(I2C_PORT_ACCEL, 1);
	zassert_equal(i2c_port_is_locked(I2C_PORT_EEPROM), 1,
		      "I2C_PORT_EEPROM not locked");
	zassert_equal(i2c_port_is_locked(I2C_PORT_ACCEL), 1,
		      "I2C_PORT_ACCEL not locked");

	/* Unlock different enum pointing the same i2c device */
	i2c_lock(I2C_PORT_EEPROM, 0);
	zassert_equal(i2c_port_is_locked(I2C_PORT_EEPROM), 0,
		      "I2C_PORT_EEPROM not locked");
	zassert_equal(i2c_port_is_locked(I2C_PORT_ACCEL), 0,
		      "I2C_PORT_ACCEL not locked");

	i2c_lock(I2C_PORT_EEPROM, 1);
	/* Verify different i2c device */
	zassert_equal(i2c_port_is_locked(I2C_PORT_USB_C1), 0,
		      "I2C_PORT_USB_C1 locked");

	i2c_lock(I2C_PORT_USB_C1, 1);
	/* Make sure i2c device is locked*/
	zassert_equal(i2c_port_is_locked(I2C_PORT_USB_C1), 1,
		      "I2C_PORT_USB_C1 locked");

	/* Another i2c device is still locked */
	i2c_lock(I2C_PORT_USB_C1, 0);
	zassert_equal(i2c_port_is_locked(I2C_PORT_EEPROM), 1,
		      "I2C_PORT_EEPROM not locked");
	i2c_lock(I2C_PORT_EEPROM, 0);
}

/* Test case main entry. */
void test_main(void)
{
	ztest_test_suite(test_i2c, ztest_user_unit_test(test_i2c_port_count),
			 ztest_user_unit_test(test_i2c_lock));
	ztest_run_test_suite(test_i2c);
}
