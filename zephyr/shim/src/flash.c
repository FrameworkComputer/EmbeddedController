/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <flash.h>
#include <kernel.h>

/* TODO(b/174873770): Add calls to Zephyr code here */

int flash_physical_write(int offset, int size, const char *data)
{
	return -ENOSYS;
}

int flash_physical_erase(int offset, int size)
{
	return -ENOSYS;
}

int flash_physical_get_protect(int bank)
{
	return -ENOSYS;
}

uint32_t flash_physical_get_protect_flags(void)
{
	return -ENOSYS;
}

int flash_physical_protect_at_boot(uint32_t new_flags)
{
	return -ENOSYS;
}

int flash_physical_protect_now(int all)
{
	return -ENOSYS;
}
