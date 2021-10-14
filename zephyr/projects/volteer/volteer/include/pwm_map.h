/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_CHROME_PWM_MAP_H
#define __ZEPHYR_CHROME_PWM_MAP_H

#include <devicetree.h>

#include "config.h"

#include "pwm/pwm.h"

/*
 * TODO(b/177452529): eliminate the dependency on enum pwm_channel
 * and configure this information directly from the device tree.
 */

#define PWM_CH_KBLIGHT		NAMED_PWM(kblight)

#endif /* __ZEPHYR_CHROME_PWM_MAP_H */
