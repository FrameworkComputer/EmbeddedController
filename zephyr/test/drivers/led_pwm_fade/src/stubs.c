// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "drivers/led.h"
#include "ec_commands.h"
#include "led_common.h"

static int mock_alt_policy = 1;

void set_board_led_alt_policy(int label)
{
	mock_alt_policy = label;
}

__override int board_led_alt_policy(void)
{
	return mock_alt_policy;
}
