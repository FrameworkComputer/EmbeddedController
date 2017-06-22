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

#define TH_ADDR 0x3c

enum cts_rc write8_test(void)
{
	int port = i2c_ports[0].port;

	i2c_write8(port, TH_ADDR, WRITE_8_OFFSET, WRITE_8_DATA);

	return CTS_RC_SUCCESS;
}

enum cts_rc write16_test(void)
{
	int port = i2c_ports[0].port;

	i2c_write16(port, TH_ADDR, WRITE_16_OFFSET, WRITE_16_DATA);

	return CTS_RC_SUCCESS;
}

enum cts_rc write32_test(void)
{
	int port = i2c_ports[0].port;

	i2c_write32(port, TH_ADDR, WRITE_32_OFFSET, WRITE_32_DATA);

	return CTS_RC_SUCCESS;
}

enum cts_rc read8_test(void)
{
	int result;
	int port = i2c_ports[0].port;

	i2c_read8(port, TH_ADDR, READ_8_OFFSET, &result);

	if (result != READ_8_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc read16_test(void)
{
	int result;
	int port = i2c_ports[0].port;

	i2c_read16(port, TH_ADDR, READ_16_OFFSET, &result);

	if (result != READ_16_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc read32_test(void)
{
	int result;
	int port = i2c_ports[0].port;

	i2c_read32(port, TH_ADDR, READ_32_OFFSET, &result);

	if (result != READ_32_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}


#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "I2C");
	task_wait_event(-1);
}
