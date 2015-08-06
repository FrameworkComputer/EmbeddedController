/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef __CROS_EC_LPC_CHIP_H
#define __CROS_EC_LPC_CHIP_H

/* Initialize host settings by interrupt */
void lpc_lreset_pltrst_handler(void);

#endif /* __CROS_EC_LPC_CHIP_H */
