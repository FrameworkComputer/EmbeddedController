/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

#include "common.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Included shimed version of gpio signal. */
#include "gpio_signal.h"

/* Include shimmed version of power signal */
#include "power/power.h"

/* Include board specific i2c mapping if I2C is enabled. */
#if defined(CONFIG_I2C)
#include "i2c/i2c.h"
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

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H */
