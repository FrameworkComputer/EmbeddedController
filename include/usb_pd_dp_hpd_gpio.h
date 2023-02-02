/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * HPD GPIO control module
 * Note: Stubs of APIs are implemented for linking if feature is not enabled
 */

#ifndef __CROS_EC_USB_DP_HPD_GPIO_H
#define __CROS_EC_USB_DP_HPD_GPIO_H

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
/*
 * Informs the AP of HPD activity through at GPIO, including toggling on IRQ if
 * necessary.
 *
 * @param[in] port		USB-C port number
 * @param[in] level		Level of the HPD line
 * @param[in] irq		IRQ pulse to the AP needed
 * @return			EC error code
 */
enum ec_error_list dp_hpd_gpio_set(int port, bool level, bool irq);

#else /* else for CONFIG_USB_PD_DP_HPD_GPIO */
static inline enum ec_error_list dp_hpd_gpio_set(int port, bool level, bool irq)
{
	return EC_SUCCESS;
}

#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
#endif /* __CROS_EC_USB_DP_HPD_GPIO_H */
