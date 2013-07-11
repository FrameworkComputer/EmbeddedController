/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wireless API for Chrome EC */

#ifndef __CROS_EC_WIRELESS_H
#define __CROS_EC_WIRELESS_H

#include "common.h"
#include "ec_commands.h"

/**
 * Set wireless switch state.
 *
 * @param flags		Enable flags from ec_commands.h (EC_WIRELESS_SWITCH_*),
 *			0 to turn all wireless off, or -1 to turn all wireless
 *			on.
 */
void wireless_enable(int flags);

#endif  /* __CROS_EC_WIRELESS_H */
