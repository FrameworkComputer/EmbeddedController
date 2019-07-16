/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI TUSB422 Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_TUSB422_H
#define __CROS_EC_USB_PD_TCPM_TUSB422_H

/* I2C interface */
#define TUSB422_I2C_ADDR_FLAGS 0x20

extern const struct tcpm_drv tusb422_tcpm_drv;

#endif /* defined(__CROS_EC_USB_PD_TCPM_TUSB422_H) */
