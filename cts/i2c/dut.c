/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cts_common.h"
#include "cts_i2c.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "watchdog.h"

#define TH_ADDR_FLAGS 0x1e

enum cts_rc write8_test(void)
{
	if (i2c_write8(i2c_ports[0].port, TH_ADDR_FLAGS,
		       WRITE8_OFF, WRITE8_DATA))
		return CTS_RC_FAILURE;
	return CTS_RC_SUCCESS;
}

enum cts_rc write16_test(void)
{
	if (i2c_write16(i2c_ports[0].port, TH_ADDR_FLAGS,
			WRITE16_OFF, WRITE16_DATA))
		return CTS_RC_FAILURE;
	return CTS_RC_SUCCESS;
}

enum cts_rc write32_test(void)
{
	if (i2c_write32(i2c_ports[0].port, TH_ADDR_FLAGS,
			WRITE32_OFF, WRITE32_DATA))
		return CTS_RC_FAILURE;
	return CTS_RC_SUCCESS;
}

enum cts_rc read8_test(void)
{
	int data;

	if (i2c_read8(i2c_ports[0].port, TH_ADDR_FLAGS,
		      READ8_OFF, &data))
		return CTS_RC_FAILURE;
	if (data != READ8_DATA) {
		CPRINTL("Expecting 0x%x but read 0x%x", READ8_DATA, data);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc read16_test(void)
{
	int data;

	if (i2c_read16(i2c_ports[0].port, TH_ADDR_FLAGS,
		       READ16_OFF, &data))
		return CTS_RC_FAILURE;
	if (data != READ16_DATA) {
		CPRINTL("Expecting 0x%x but read 0x%x", READ16_DATA, data);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc read32_test(void)
{
	int data;

	if (i2c_read32(i2c_ports[0].port, TH_ADDR_FLAGS,
		       READ32_OFF, &data))
		return CTS_RC_FAILURE;
	if (data != READ32_DATA) {
		CPRINTL("Read 0x%x expecting 0x%x", data, READ32_DATA);
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}


#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "I2C");
	task_wait_event(-1);
}
