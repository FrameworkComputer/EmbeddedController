/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB30532 USB port switch driver.
 */

#ifndef __CROS_EC_PI3USB30532_H
#define __CROS_EC_PI3USB30532_H

#include "usb_pd.h"

/* USB switch registers */
#define PI3USB30532_REG_ADDR 0x00
#define PI3USB30532_REG_VENDOR 0x01
#define PI3USB30532_REG_CONTROL 0x02
/* Control register field */
#define PI3USB30532_CTRL_MASK 0x7
#define PI3USB30532_CTRL_RSVD 0
/* Switch vendor ID  */
#define PI3USB30532_VENDOR_ID 0

/* PI3USB30532 control flags */
#define PI3USB30532_BIT_SWAP BIT(0)
#define PI3USB30532_BIT_DP BIT(1)
#define PI3USB30532_BIT_USB BIT(2)

/* PI3USB30532 modes */
/* Power down, switch open */
#define PI3USB30532_MODE_POWERDOWN 0
/* Keep power on, switch open */
#define PI3USB30532_MODE_POWERON 1
/* 4-lane DP 1.2
 * dp0~3 : rx2, tx2, tx1, rx1
 * hpd+/-: rfu1, rfu2
 */
#define PI3USB30532_MODE_DP PI3USB30532_BIT_DP
/* 4-lane DP 1.2 swap
 * dp0~3 : rx1, tx1, tx2, rx2
 * hpd+/-: rfu2, rfu1
 */
#define PI3USB30532_MODE_DP_SWAP (PI3USB30532_MODE_DP | PI3USB30532_BIT_SWAP)
/* USB3
 * tx/rx : tx1, rx1
 */
#define PI3USB30532_MODE_USB PI3USB30532_BIT_USB
/* USB3 swap
 * tx/rx : tx2, rx2
 */
#define PI3USB30532_MODE_USB_SWAP (PI3USB30532_MODE_USB | PI3USB30532_BIT_SWAP)
/* 2-lane DP 1.2 + USB3
 * tx/rx : tx1, rx1
 * dp0~1 : rx2, tx2
 * hpd+/-: rfu1, rfu2
 */
#define PI3USB30532_MODE_DP_USB (PI3USB30532_BIT_DP | PI3USB30532_BIT_USB)
/* 2-lane DP 1.2 + USB3, swap
 * tx/rx : tx2, rx2
 * dp0-1 : rx1, tx1
 * hpd+/-: rfu2, rfu1
 */
#define PI3USB30532_MODE_DP_USB_SWAP (PI3USB30532_MODE_DP_USB | \
				      PI3USB30532_BIT_SWAP)

#endif /* __CROS_EC_PI3USB30532_H */
