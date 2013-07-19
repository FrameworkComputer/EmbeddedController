/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Embed firmware version number in the binary */

#include <stdint.h>
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
