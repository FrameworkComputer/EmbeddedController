/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

/**
 * Handle an exception and reboot. The parameters after 'vector' are
 * convenientely in the same order as pushed by hardwared during a
 * processor exception.
 */
__noreturn void exception_panic(uint32_t vector, uint32_t errorcode,
				uint32_t eip, uint32_t cs, uint32_t eflags);
