
/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __SCP_WATCHDOG_H
#define __SCP_WATCHDOG_H

#include "watchdog.h"

void watchdog_disable(void);
void watchdog_enable(void);

#endif /* __SCP_WATCHDOG_H */
