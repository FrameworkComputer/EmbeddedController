/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPIO_H
#define GPIO_H

#include "MCHP_MEC172x.h"

#include <stdint.h>

void gpio_pin_ctrl1_reg_write(uint32_t pin, uint32_t data);

#endif /* #ifndef GPIO_H */
