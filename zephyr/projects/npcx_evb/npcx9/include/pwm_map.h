/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_PWM_MAP_H
#define __ZEPHYR_PWM_MAP_H

#include <devicetree.h>

#include "pwm/pwm.h"

#define PWM_CH_FAN		NAMED_PWM(fan)
#define PWM_CH_KBLIGHT		NAMED_PWM(kblight)

#endif /* __ZEPHYR_PWM_MAP_H */
