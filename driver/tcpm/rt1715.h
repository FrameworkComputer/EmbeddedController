/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Richtek RT1715 Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_RT1715_H
#define __CROS_EC_USB_PD_TCPM_RT1715_H

#include "driver/tcpm/rt1715_public.h"

#define RT1715_REG_VENDOR_5 0x9B
#define RT1715_REG_VENDOR_5_SHUTDOWN_OFF BIT(5)
#define RT1715_REG_VENDOR_5_ENEXTMSG BIT(4)

#define RT1715_REG_VENDOR_7 0xA0
#define RT1715_REG_VENDOR_7_SOFT_RESET BIT(0)

#endif /* defined(__CROS_EC_USB_PD_TCPM_RT1715_H) */
