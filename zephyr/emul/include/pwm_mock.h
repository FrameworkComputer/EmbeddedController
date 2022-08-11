/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_INCLUDE_PWM_MOCK_H
#define __EMUL_INCLUDE_PWM_MOCK_H

#include <zephyr/device.h>

/*
 * Get pwm duty cycle
 *
 * @param dev		pointer to hte pwm device
 * @param channel	channel id
 *
 * @return duty		duty cycle in range [0, 100] or negative on error.
 */
int pwm_mock_get_duty(const struct device *dev, uint32_t channel);

#endif /*__EMUL_INCLUDE_PWM_MOCK_H */
