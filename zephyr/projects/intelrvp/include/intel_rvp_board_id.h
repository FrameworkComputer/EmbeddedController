/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __INTEL_RVP_BOARD_ID_H
#define __INTEL_RVP_BOARD_ID_H

#include <zephyr/drivers/gpio.h>

extern const struct gpio_dt_spec bom_id_config[];

extern const struct gpio_dt_spec fab_id_config[];

extern const struct gpio_dt_spec board_id_config[];

#endif /* __INTEL_RVP_BOARD_ID_H */
