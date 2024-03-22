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

/*
 * This function looks for signs that a debugger was attached. If we
 * see that a debugger was attached, we know that the chip's security features
 * may function as if the debugger is still attached.
 *
 * This should be true while a debugger is actively connected, too.
 */
__override_proto bool debugger_was_connected(void);

#endif /* __CROS_EC_DEBUG_H */
