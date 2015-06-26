/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8740 USB port switch driver.
 */

#ifndef __CROS_EC_PS8740_H
#define __CROS_EC_PS8740_H

/* Mode register for setting mux */
#define PS8740_REG_MODE 0x00
#define PS8740_MODE_POLARITY_INVERTED (1 << 4)
#define PS8740_MODE_USB_ENABLED (1 << 5)
#define PS8740_MODE_DP_ENABLED (1 << 6)
#define PS8740_MODE_POWER_DOWN (1 << 7)

/* Status register for checking mux state */
#define PS8740_REG_STATUS 0x09
#define PS8740_STATUS_POLARITY_INVERTED (1 << 2)
#define PS8740_STATUS_USB_ENABLED (1 << 3)
#define PS8740_STATUS_DP_ENABLED (1 << 4)
#define PS8740_STATUS_HPD_ASSERTED (1 << 7)

/* Chip ID / revision registers and expected fused values */
#define PS8740_REG_REVISION_ID1 0xf0
#define PS8740_REVISION_ID1 0x00
#define PS8740_REG_REVISION_ID2 0xf1
#define PS8740_REVISION_ID2 0x0a
#define PS8740_REG_CHIP_ID1 0xf2
#define PS8740_CHIP_ID1 0x40
#define PS8740_REG_CHIP_ID2 0xf3
#define PS8740_CHIP_ID2 0x87

#endif /* __CROS_EC_PS8740_H */
