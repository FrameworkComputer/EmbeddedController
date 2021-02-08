/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TI TUSB422 Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_TUSB422_H
#define __CROS_EC_USB_PD_TCPM_TUSB422_H

#include "driver/tcpm/tusb422_public.h"

#define TUSB422_REG_VENDOR_INTERRUPTS_STATUS 0x90
#define TUSB422_REG_VENDOR_INTERRUPTS_STATUS_FRS_RX BIT(0)

#define TUSB422_REG_VENDOR_INTERRUPTS_MASK 0x92
#define TUSB422_REG_VENDOR_INTERRUPTS_MASK_FRS_RX BIT(0)

#define TUSB422_REG_PHY_BMC_RX_CTRL 0x96
#define TUSB422_REG_PHY_BMC_RX_CTRL_FRS_RX_EN BIT(3)

#endif /* defined(__CROS_EC_USB_PD_TCPM_TUSB422_H) */
