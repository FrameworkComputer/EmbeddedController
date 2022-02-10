/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

#include <devicetree.h>
#include "common.h"

/* Included shimed version of gpio signal. */
#include "gpio_signal.h"

/* Include shimmed version of power signal */
#include "power/power.h"

/* Include board specific gpio mapping/aliases if named_pgios node exists */
#if !defined(TEST_BUILD) && DT_NODE_EXISTS(DT_PATH(named_gpios))
#include "gpio_map.h"
#endif

/* Include board specific i2c mapping if I2C is enabled. */
#if defined(CONFIG_I2C)
#include "i2c/i2c.h"
#endif

#ifdef CONFIG_PWM
#include "pwm/pwm.h"
#endif

/* Include board specific sensor configuration if motionsense is enabled */
#ifdef CONFIG_MOTIONSENSE
#include "motionsense_sensors_defs.h"
#endif

/*
 * Should generate enums for each charger.
 */
#ifdef CONFIG_PLATFORM_EC_OCPC
#include "charger_enum.h"
#endif

#endif  /* __BOARD_H */
