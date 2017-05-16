/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_PS8743_H
#define __CROS_EC_PS8743_H

/* Mode register for setting mux */
#define PS8743_REG_MODE 0x00
#define PS8743_MODE_POLARITY_INVERTED (1 << 2)
#define PS8743_MODE_FLIP_PIN_ENABLED  (1 << 3)
#define PS8743_MODE_USB_ENABLED       (1 << 4)
#define PS8743_MODE_CE_USB_ENABLED    (1 << 5)
#define PS8743_MODE_DP_ENABLED        (1 << 6)
#define PS8743_MODE_CE_DP_ENABLED     (1 << 7)
/* To reset the state machine to default */
#define PS8743_MODE_POWER_DOWN    (PS8743_MODE_CE_USB_ENABLED |  \
				   PS8743_MODE_CE_DP_ENABLED)

/* Status register for checking mux state */
#define PS8743_REG_STATUS 0x09
#define PS8743_STATUS_POLARITY_INVERTED (1 << 2)
#define PS8743_STATUS_USB_ENABLED       (1 << 3)
#define PS8743_STATUS_DP_ENABLED        (1 << 4)
#define PS8743_STATUS_HPD_ASSERTED      (1 << 7)

/* Chip ID / revision registers and expected fused values */
#define PS8743_REG_REVISION_ID1 0xf0
#define PS8743_REVISION_ID1     0x01
#define PS8743_REG_REVISION_ID2 0xf1
#define PS8743_REVISION_ID2     0x0b
#define PS8743_REG_CHIP_ID1     0xf2
#define PS8743_CHIP_ID1         0x41
#define PS8743_REG_CHIP_ID2     0xf3
#define PS8743_CHIP_ID2         0x87

#endif /* __CROS_EC_PS8743_H */
