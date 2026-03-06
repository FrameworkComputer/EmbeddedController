// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver/led/max695x.h"
#include "drivers/led.h"
#include "ec_commands.h"
#include "led_common.h"

/* MAX695x test configuration */
#ifndef PORT80_I2C_ADDR
#define PORT80_I2C_ADDR MAX695X_I2C_ADDR1_FLAGS
#endif

static int mock_alt_policy = 1;

void set_board_led_alt_policy(int label)
{
	mock_alt_policy = label;
}

__override int board_led_alt_policy(void)
{
	return mock_alt_policy;
}
