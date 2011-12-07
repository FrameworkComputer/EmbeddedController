/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#ifndef __CROS_EC_VBOOT_H
#define __CROS_EC_VBOOT_H

#include "common.h"

/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int vboot_pre_init(void);

/* Initializes the module. */
int vboot_init(void);

#endif  /* __CROS_EC_VBOOT_H */
