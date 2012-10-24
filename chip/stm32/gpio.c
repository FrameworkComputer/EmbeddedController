/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common GPIO module for Chrome EC */

#include "common.h"
#include "gpio.h"


const char *gpio_get_name(enum gpio_signal signal)
{
	return gpio_list[signal].name;
}

