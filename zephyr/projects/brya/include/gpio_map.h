/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <zephyr/devicetree.h>
#include <gpio_signal.h>

#define GPIO_ENTERING_RW	GPIO_UNIMPLEMENTED

#define GPIO_SEQ_EC_DSW_PWROK GPIO_PG_EC_DSW_PWROK

/* TODO(fabiobaltieri): make this a named-temp-sensors property, deprecate the
 * Kconfig option.
 */
#define GPIO_TEMP_SENSOR_POWER GPIO_EN_S5_RAILS

#endif /* __ZEPHYR_GPIO_MAP_H */
