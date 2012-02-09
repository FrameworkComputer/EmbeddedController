/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* JTAG interface for Chrome EC */

#ifndef __CROS_EC_JTAG_H
#define __CROS_EC_JTAG_H

#include "common.h"

/* Pre-initializes the module. */
int jtag_pre_init(void);

#endif  /* __CROS_EC_JTAG_H */
