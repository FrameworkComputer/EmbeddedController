/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "i2c.h"
#include "i2c/i2c.h"

#include <zephyr/devicetree.h>
#include <zephyr/ztest.h>

/* Unused: required for shimming i2c. */
void watchdog_reload(void)
{
}

ZTEST_USER(i2c, test_i2c_port_count)
{
	zassert_equal(I2C_PORT_COUNT, 2,
		      "I2C_PORT_COUNT expected to be 2 but was %d",
		      I2C_PORT_COUNT);
}

ZTEST_USER(i2c, test_i2c_lock_invalid_port)
{
	i2c_lock(-1, 1);
	zassert_equal(i2c_port_is_locked(-1), 0,
		      "Negative I2C port locked, but should have failed");

	i2c_lock(INT_MAX, 1);
	zassert_equal(i2c_port_is_locked(INT_MAX), 0,
		      "MAX_INT I2C port locked, but should have failed");
}

ZTEST_USER(i2c, test_i2c_lock)
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

ZTEST_SUITE(i2c, NULL, NULL, NULL, NULL, NULL);
