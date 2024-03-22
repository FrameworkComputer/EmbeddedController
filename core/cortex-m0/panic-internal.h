/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PANIC_INTERNAL_H
#define __CROS_EC_PANIC_INTERNAL_H

#include "common.h"

__noreturn void exception_panic(void) __attribute__((naked));

#endif /* __CROS_EC_PANIC_INTERNAL_H */
