/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "console.h"
#include "cpu.h"
#include "flash.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
}

void system_pre_init(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
}

void system_reset(int flags)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
}

int system_set_scratchpad(uint32_t value)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

uint32_t system_get_scratchpad(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

const char *system_get_chip_vendor(void)
{
	return "ite";
}

const char *system_get_chip_name(void)
{
	return "it83xx";
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_vbnvcontext(uint8_t *block)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return EC_SUCCESS;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return EC_SUCCESS;
}

int system_set_console_force_enabled(int val)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

int system_get_console_force_enabled(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}
