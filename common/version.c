/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Embed firmware version number in the binary */

#include "common.h"
#include "compile_time_macros.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "stddef.h"
#include "system.h"

#include <stdint.h>

#ifndef CROS_FWID32
#define CROS_FWID32 ""
#endif

BUILD_ASSERT(CONFIG_ROLLBACK_VERSION >= 0);
BUILD_ASSERT(CONFIG_ROLLBACK_VERSION <= INT32_MAX);

const struct image_data __keep current_image_data FIXED_SECTION("ver") = {
	.cookie1 = CROS_EC_IMAGE_DATA_COOKIE1,
	.version = CROS_EC_VERSION32,
#ifndef TEST_BUILD
	.size = (const uintptr_t)&__image_size,
#endif
	.rollback_version = CONFIG_ROLLBACK_VERSION,
	.cookie2 = CROS_EC_IMAGE_DATA_COOKIE2,
	.cros_fwid = CROS_FWID32,
	.cookie3 = CROS_EC_IMAGE_DATA_COOKIE3,
};
BUILD_ASSERT(sizeof(CROS_EC_VERSION32) <= 32);
BUILD_ASSERT(sizeof(CROS_FWID32) <= 32);

#ifdef CONFIG_CROS_FWID_VERSION
const char build_info[] __keep __attribute__((section(".rodata.buildinfo"))) =
	VERSION " " CROS_FWID32 " " DATE " " BUILDER;
#else
const char build_info[] __keep __attribute__((section(".rodata.buildinfo"))) =
	VERSION " " DATE " " BUILDER;
#endif

/* LCOV_EXCL_START - this function doesn't work in GitLab */
static int get_num_commits(const struct image_data *data)
{
	int numperiods = 0;
	int ret = 0;
	size_t i;

	/* Version string format is name_major.branch.commits-hash[dirty] */
	for (i = 0; i < sizeof(data->version); i++) {
		if (data->version[i] == '.') {
			numperiods++;
			if (numperiods == 2)
				break;
		}
	}

	i++;
	for (; i < sizeof(data->version); i++) {
		int d;
		if (data->version[i] == '-')
			break;
		ret *= 10;
		d = data->version[i] - '0';
		if (d < 0 || d > 9)
			return 0;
		ret += d;
	}

	return (i == sizeof(data->version) ? 0 : ret);
}
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START - this function doesn't work in GitLab */
int ver_get_num_commits(enum ec_image copy)
{
	const struct image_data *data;
	if (IS_ENABLED(CONFIG_COMMON_RUNTIME))
		data = system_get_image_data(copy);
	else
		data = &current_image_data;
	return data ? get_num_commits(data) : 0;
}
/* LCOV_EXCL_STOP */
