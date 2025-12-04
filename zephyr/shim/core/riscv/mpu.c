// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hooks.h"
#include "system.h"

#include <zephyr/arch/riscv/pmp.h>

static void prepare_for_sysjump_to_ec(void)
{
	/*
	 * Clear all Physical Memory Protection (PMP) entries before jumping to
	 * the new image. When CONFIG_HW_STACK_PROTECTION is enabled, the
	 * current image's PMP configuration for stack boundaries can interfere
	 * with the stack setup of the new image, leading to crashes. Clearing
	 * ensures the new image starts with a clean PMP state.
	 */
	z_riscv_pmp_clear_all();
}
DECLARE_HOOK(HOOK_SYSJUMP, prepare_for_sysjump_to_ec, HOOK_PRIO_LAST);
