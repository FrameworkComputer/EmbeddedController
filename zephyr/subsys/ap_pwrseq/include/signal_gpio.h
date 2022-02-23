/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_PWRSEQ_SIGNAL_GPIO_H__
#define __AP_PWRSEQ_SIGNAL_GPIO_H__

#define PWR_SIG_TAG_GPIO	PWR_GPIO_

/*
 * Generate enums for the GPIOs.
 * These enums are only used internally
 * to assign an index to each signal that is specific
 * to the source.
 */

#define TAG_GPIO(tag, name) DT_CAT(tag, name)

#define PWR_GPIO_ENUM(id) TAG_GPIO(PWR_SIG_TAG_GPIO, PWR_SIGNAL_ENUM(id)),

enum pwr_sig_gpio {
#if HAS_GPIO_SIGNALS
DT_FOREACH_STATUS_OKAY(intel_ap_pwrseq_gpio, PWR_GPIO_ENUM)
#endif
	PWR_SIG_GPIO_COUNT
};

#undef	PWR_GPIO_ENUM
#undef	TAG_GPIO

/**
 * @brief Get the value of the GPIO power signal.
 *
 * @param signal The power_signal_gpios value to get.
 * @return the current value of the power signal.
 */
int power_signal_gpio_get(enum pwr_sig_gpio gpio);

/**
 * @brief Set the output of this GPIO power signal.
 *
 * @param signal The GPIO to set.
 * @param value The output value to set it to.
 * @return 0 is successful
 * @return negative If output cannot be set.
 */
int power_signal_gpio_set(enum pwr_sig_gpio gpio, int value);

/**
 * @brief Enable the GPIO interrupt
 *
 * @param signal The power_signal_gpios to enable.
 * @return 0 if successful
 * @return -error if failed
 */
int power_signal_gpio_enable_int(enum pwr_sig_gpio gpio);

/**
 * @brief Disable the GPIO interrupt
 *
 * @param signal The power_signal_gpios to disable.
 * @return 0 if successful
 * @return -error if failed
 */
int power_signal_gpio_disable_int(enum pwr_sig_gpio gpio);

/**
 * @brief Initialize the GPIOs for the power signals.
 */
void power_signal_gpio_init(void);

#endif /* __AP_PWRSEQ_SIGNAL_GPIO_H__ */
