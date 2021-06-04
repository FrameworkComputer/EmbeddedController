/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_PS8743_H
#define __CROS_EC_PS8743_H

#include "usb_mux.h"
#include "usb_mux/ps8743_public.h"

/* Status register for checking mux state */
#define PS8743_REG_STATUS 0x09
#define PS8743_STATUS_POLARITY_INVERTED BIT(2)
#define PS8743_STATUS_USB_ENABLED       BIT(3)
#define PS8743_STATUS_DP_ENABLED        BIT(4)
#define PS8743_STATUS_HPD_ASSERTED      BIT(7)

/* Chip ID / revision registers and expected fused values */
#define PS8743_REG_REVISION_ID1 0xf0
#define PS8743_REG_REVISION_ID2 0xf1
#define PS8743_REG_CHIP_ID1     0xf2
#define PS8743_REG_CHIP_ID2     0xf3
#define PS8743_REVISION_ID1_0   0x00
#define PS8743_REVISION_ID1_1   0x01
#define PS8743_REVISION_ID2     0x0b
#define PS8743_CHIP_ID1         0x41
#define PS8743_CHIP_ID2         0x87

#endif /* __CROS_EC_PS8743_H */
