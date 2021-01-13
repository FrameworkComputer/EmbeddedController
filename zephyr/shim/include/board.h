/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

#include <devicetree.h>

/* Included shimed version of gpio signal. */
#include "gpio_signal.h"

/* Include board specific gpio mapping/aliases if named_pgios node exists */
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
#include "gpio_map.h"
#endif

/* Include board specific i2c mapping if I2C is enabled. */
#if defined(CONFIG_I2C) && !defined(CONFIG_ZTEST)
#include "i2c_map.h"
#endif

#ifdef CONFIG_PWM
#include "pwm_map.h"
#endif

/* Include board specific sensor configuration if motionsense is enabled */
#ifdef CONFIG_MOTIONSENSE
#include "sensor_map.h"
#endif

#endif  /* __BOARD_H */
