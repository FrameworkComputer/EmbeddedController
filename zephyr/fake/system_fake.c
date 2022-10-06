/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <setjmp.h>

#include "system.h"
#include "system_fake.h"

static enum ec_image shrspi_image_copy = EC_IMAGE_RO;

/* setjmp environment to use for reboot (NULL if none) */
static jmp_buf *jump_env;

void system_fake_setenv(jmp_buf *env)
{
	jump_env = env;
}

void system_jump_to_booter(void)
{
	if (jump_env)
		longjmp(*jump_env, 1);
}

uint32_t system_get_lfw_address(void)
{
	uint32_t jump_addr = (uint32_t)system_jump_to_booter;

	return jump_addr;
}

enum ec_image system_get_shrspi_image_copy(void)
{
	return shrspi_image_copy;
}

void system_set_shrspi_image_copy(enum ec_image new_image_copy)
{
	shrspi_image_copy = new_image_copy;
}

void system_set_image_copy(enum ec_image copy)
{
}
