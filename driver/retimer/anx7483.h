/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7483: Active redriver with linear equilzation
 */

#ifndef __CROS_EC_USB_RETIMER_ANX7483_H
#define __CROS_EC_USB_RETIMER_ANX7483_H

#define ANX7483_ANALOG_STATUS_CTRL	0x07
#define ANX7483_CTRL_REG_BYPASS_EN	BIT(5)
#define ANX7483_CTRL_REG_EN		BIT(4)
#define ANX7483_CTRL_FLIP_EN		BIT(2)
#define ANX7483_CTRL_DP_EN		BIT(1)
#define ANX7483_CTRL_USB_EN		BIT(0)

#endif /* __CROS_EC_USB_RETIMER_ANX7483_H */
