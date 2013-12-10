/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power button API for Chrome EC */

#ifndef __CROS_EC_AP_HANG_DETECT_H
#define __CROS_EC_AP_HANG_DETECT_H

#include "common.h"

/**
 * If the hang detect timers were started and can be stopped by any host
 * command, stop them.  This is intended to be called by the the host command
 * module.
 */
void hang_detect_stop_on_host_command(void);

#endif  /* __CROS_EC_AP_HANG_DETECT_H */
