/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

#include <devicetree.h>

/* Included shimed version of gpio signal. */
#include "gpio_signal.h"

/* Include shimmed version of power signal */
#include "power/power.h"

/* Include board specific gpio mapping/aliases if named_pgios node exists */
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
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
 * If there are multiple chargers, the number of
 * chargers (CHARGER_NUM) does not get defined, which causes
 * board_get_charger_chip_count in common/charger.c to fail.
 * In the legacy system, this is instead defined in the board.h
 * board specific header.  For zephyr, there is no such board specific
 * header, so to work around this, if there are multiple chargers, define
 * this value as a default, and assume there will be an override
 * function to get the correct value.
 */
#ifdef CONFIG_PLATFORM_EC_OCPC
#define CHARGER_NUM CONFIG_USB_PD_PORT_MAX_COUNT
#endif

#endif  /* __BOARD_H */
