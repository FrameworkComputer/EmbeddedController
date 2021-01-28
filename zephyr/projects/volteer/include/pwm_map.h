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
#define PWM_CH_LED1_BLUE	NAMED_PWM(led1_blue)
#define PWM_CH_LED2_GREEN	NAMED_PWM(led2_green)
#define PWM_CH_LED3_RED		NAMED_PWM(led3_red)
#define PWM_CH_LED4_SIDESEL	NAMED_PWM(led3_sidesel)

#define PWM_CH_KBLIGHT		NAMED_PWM(kblight)

#endif /* __ZEPHYR_CHROME_PWM_MAP_H */
