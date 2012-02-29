/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Version number for Chrome EC */

#ifndef __CROS_EC_VERSION_H
#define __CROS_EC_VERSION_H

#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)

#define CROS_EC_VERSION_COOKIE1 0xce112233
#define CROS_EC_VERSION_COOKIE2 0xce445566

#define CROS_EC_VERSION STRINGIFY(BOARD) "_" VERSION
#define CROS_EC_BUILD_INFO CROS_EC_VERSION " " DATE " " BUILDER

struct version_struct {
	uint32_t cookie1;
	char version[32];
	uint32_t cookie2;
} __attribute__ ((packed));

extern const struct version_struct version_data;
extern const char build_info[];

#endif  /* __CROS_EC_VERSION_H */
