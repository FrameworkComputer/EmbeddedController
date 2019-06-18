/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Embed firmware version number in the binary */

#include <stdint.h>
#include "common.h"
#include "compile_time_macros.h"
#include "ec_version.h"
#include "version.h"

BUILD_ASSERT(CONFIG_ROLLBACK_VERSION >= 0);
BUILD_ASSERT(CONFIG_ROLLBACK_VERSION <= INT32_MAX);

const struct image_data __keep current_image_data
	__attribute__((section(".rodata.ver"))) = {
	.cookie1 = CROS_EC_IMAGE_DATA_COOKIE1,
	.version = CROS_EC_VERSION32,
#ifndef TEST_BUILD
	.size = (const uintptr_t)&__image_size,
#endif
	.rollback_version = CONFIG_ROLLBACK_VERSION,
	.cookie2 = CROS_EC_IMAGE_DATA_COOKIE2,
};

const char build_info[] __keep __attribute__((section(".rodata.buildinfo"))) =
	VERSION " " DATE " " BUILDER;

uint32_t ver_get_numcommits(void)
{
	int i;
	int numperiods = 0;
	uint32_t ret = 0;

	/*
	 * Version string is formatted like:
	 * name_major.branch.numcommits-hash[dirty]
	 * we want to return the numcommits as an int.
	 */
	for (i = 0; i < 32; i++) {
		if (current_image_data.version[i] == '.') {
			numperiods++;
			if (numperiods == 2)
				break;
		}
	}

	i++;
	for (; i < 32; i++) {
		if (current_image_data.version[i] == '-')
			break;
		ret *= 10;
		ret += current_image_data.version[i] - '0';
	}

	return (i == 32 ? 0 : ret);
}
