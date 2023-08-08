/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_GPIO_CONTROLLER_MOCK_H_
#define ZEPHYR_INCLUDE_EMUL_GPIO_CONTROLLER_MOCK_H_

/**
 * Return the number of calls made to the GPIO mock controller .pin_configure
 * routine.
 *
 * @param port Pointer to device structure for the driver instance
 * @retval -EINVAL if the GPIO controller has not been initialized.  Otherwise
 * returns the total count of calls to .pin_configure.
 */
int gpio_mock_controller_pin_configure_call_count(const struct device *port);

#endif /* ZEPHYR_INCLUDE_EMUL_GPIO_CONTROLLER_MOCK_H_ */
