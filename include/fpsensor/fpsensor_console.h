/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_CONSOLE_H
#define __CROS_EC_FPSENSOR_FPSENSOR_CONSOLE_H

#include "console.h"

#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_CONSOLE_H */
