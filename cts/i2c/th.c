/* Copyright 2016 The ChromiumOS Authors
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

#include <string.h>

static uint8_t inbox[I2C_MAX_HOST_PACKET_SIZE + 2];
static char data_received;

void i2c_data_received(int port, uint8_t *buf, int len)
{
	memcpy(inbox, buf, len);
	data_received = 1;
}

/* CTS I2C protocol implementation */
int i2c_set_response(int port, uint8_t *buf, int len)
{
	switch (buf[0]) {
	case READ8_OFF:
		buf[0] = READ8_DATA;
		return 1;
	case READ16_OFF:
		buf[0] = READ16_DATA & 0xFF;
		buf[1] = (READ16_DATA >> 8) & 0xFF;
		return 2;
	case READ32_OFF:
		buf[0] = READ32_DATA & 0xFF;
		buf[1] = (READ32_DATA >> 8) & 0xFF;
		buf[2] = (READ32_DATA >> 16) & 0xFF;
		buf[3] = (READ32_DATA >> 24) & 0xFF;
		return 4;
	default:
		return 0;
	}
}

static int wait_for_in_flag(uint32_t timeout_ms)
{
	uint64_t start_time, end_time;

	start_time = get_time().val;
	end_time = start_time + timeout_ms * 1000;

	while (get_time().val < end_time) {
		if (data_received)
			return 0;
		crec_msleep(5);
		watchdog_reload();
	}
	return 1;
}

void clean_state(void)
{
	memset(inbox, 0, sizeof(inbox));
	data_received = 0;
}

enum cts_rc write8_test(void)
{
	int in;

	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != WRITE8_OFF)
		return CTS_RC_FAILURE;
	in = inbox[1];
	if (in != WRITE8_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc write16_test(void)
{
	int in;

	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != WRITE16_OFF)
		return CTS_RC_FAILURE;
	in = inbox[2] << 8 | inbox[1] << 0;
	if (in != WRITE16_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc write32_test(void)
{
	int in;

	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != WRITE32_OFF)
		return CTS_RC_FAILURE;
	in = inbox[4] << 24 | inbox[3] << 16 | inbox[2] << 8 | inbox[1];
	if (in != WRITE32_DATA)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc read8_test(void)
{
	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != READ8_OFF)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc read16_test(void)
{
	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != READ16_OFF)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

enum cts_rc read32_test(void)
{
	if (wait_for_in_flag(100))
		return CTS_RC_TIMEOUT;
	if (inbox[0] != READ32_OFF)
		return CTS_RC_FAILURE;

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	cts_main_loop(tests, "I2C");
	task_wait_event(-1);
}
