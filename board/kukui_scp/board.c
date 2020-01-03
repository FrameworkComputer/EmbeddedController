/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kukui SCP configuration */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Build GPIO tables */
#include "gpio_list.h"

