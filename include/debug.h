/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DEBUG_H
#define __CROS_EC_DEBUG_H

#include "common.h"
#include "stdbool.h"

/*
 * Indicates if a debugger is actively connected.
 */
__override_proto bool debugger_is_connected(void);

#endif /* __CROS_EC_DEBUG_H */
