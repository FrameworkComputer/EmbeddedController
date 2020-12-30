/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI TUSB422 Type-C port controller */

#ifndef __CROS_EC_DRIVER_TCPM_TUSB422_PUBLIC_H
#define __CROS_EC_DRIVER_TCPM_TUSB422_PUBLIC_H

/* I2C interface */
#define TUSB422_I2C_ADDR_FLAGS 0x20

extern const struct tcpm_drv tusb422_tcpm_drv;

#endif /* __CROS_EC_DRIVER_TCPM_TUSB422_PUBLIC_H */
