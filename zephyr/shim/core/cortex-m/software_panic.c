/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Content of this file are taken directly from
 * platform/ec/core/cortex-m/panic.c. The code is replicated deliberately to
 * allow future refactors into Zephyr specific APIs.
 */

#include "common.h"
#include "panic.h"

void software_panic(uint32_t reason, uint32_t info)
{
	/* TODO(b:180422087) Zephyrize this. */
	__asm__("mov " STRINGIFY(SOFTWARE_PANIC_INFO_REG) ", %0\n"
		"mov " STRINGIFY(SOFTWARE_PANIC_REASON_REG) ", %1\n"
		"bl exception_panic\n"
		: : "r"(info), "r"(reason));
	__builtin_unreachable();
}
