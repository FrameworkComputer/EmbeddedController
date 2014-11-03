/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Embed firmware version number in the binary */

#include <stdint.h>
#include "common.h"
#include "ec_date.h"
#include "ec_version.h"
#include "version.h"

const struct version_struct version_data
	__attribute__((section(".rodata.ver"))) = {
	CROS_EC_VERSION_COOKIE1,
	CROS_EC_VERSION32,
	CROS_EC_VERSION_COOKIE2
};

const char build_info[] __attribute__((section(".rodata.buildinfo")))  =
	CROS_EC_VERSION " " DATE " " BUILDER;

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
		if (version_data.version[i] == '.') {
			numperiods++;
			if (numperiods == 2)
				break;
		}
	}

	i++;
	for (; i < 32; i++) {
		if (version_data.version[i] == '-')
			break;
		ret *= 10;
		ret += version_data.version[i] - '0';
	}

	return (i == 32 ? 0 : ret);
}

