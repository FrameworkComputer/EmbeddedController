/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_POWERON_CONF_H
#define __CROS_POWERON_CONF_H

#define USB_PORT_MUX_TO_DUT BIT(0)
#define USB_PORT_POWER_EN BIT(1)
#define USB_PORT_MUX_EN BIT(2)

/* Read config and set usb ports as expected in poweron config */
void apply_poweron_conf(void);

#endif /* __CROS_POWERON_CONF_H */
