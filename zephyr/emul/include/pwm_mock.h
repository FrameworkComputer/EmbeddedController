/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_INCLUDE_PWM_MOCK_H
#define __EMUL_INCLUDE_PWM_MOCK_H

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

/*
 * Get pwm duty cycle
 *
 * @param dev		pointer to hte pwm device
 * @param channel	channel id
 *
 * @return duty		duty cycle in range [0, 100] or negative on error.
 */
int pwm_mock_get_duty(const struct device *dev, uint32_t channel);

/**
 * @brief Get the flags the PWM driver was set with. See the following header
 *        in upstream Zephyr for possible values:
 *        `include/zephyr/dt-bindings/pwm/pwm.h`
 *
 * @param dev Pointer to PWM device
 * @param channel Unused
 * @return pwm_flags_t PWM flags
 */
pwm_flags_t pwm_mock_get_flags(const struct device *dev, uint32_t channel);

#endif /*__EMUL_INCLUDE_PWM_MOCK_H */
