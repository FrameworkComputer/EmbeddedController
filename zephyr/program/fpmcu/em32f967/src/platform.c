/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "write_protect.h"

/* TODO(b/446109143): Change based on the final solution of WP state. */
static int disable_write_protect(void)
{
	disable_write_protect_external();

	return 0;
}
SYS_INIT(disable_write_protect, POST_KERNEL, 0);

/* Actual implementation is placed in the private repository. */
__weak void chip_enter_bootloader(uint8_t mode)
{
}
