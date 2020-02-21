/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP5 USB/DP Mux.
 */

#ifndef __CROS_EC_USB_MUX_AMD_FP5_H
#define __CROS_EC_USB_MUX_AMD_FP5_H

#define AMD_FP5_MUX_I2C_ADDR_FLAGS	0x5C

#define AMD_FP5_MUX_SAFE		0x00
#define AMD_FP5_MUX_USB			0x02
#define AMD_FP5_MUX_USB_INVERTED	0x11
#define AMD_FP5_MUX_DOCK		0x06
#define AMD_FP5_MUX_DOCK_INVERTED	0x19
#define AMD_FP5_MUX_DP			0x0C
#define AMD_FP5_MUX_DP_INVERTED		0x1C

#endif /* __CROS_EC_USB_MUX_AMD_FP5_H */
