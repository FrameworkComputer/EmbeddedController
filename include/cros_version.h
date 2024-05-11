/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Version number for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#ifndef __CROS_EC_VERSION_H
#define __CROS_EC_VERSION_H

#include "common.h"
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CROS_EC_IMAGE_DATA_COOKIE1 0xce778899
#define CROS_EC_IMAGE_DATA_COOKIE2 0xceaabbdd
#define CROS_EC_IMAGE_DATA_COOKIE3 0xceeeff00
#define CROS_EC_IMAGE_DATA_COOKIE3_MASK GENMASK(31, 8)
#define CROS_EC_IMAGE_DATA_COOKIE3_VERSION GENMASK(7, 0)

#define CROS_FWID_MISSING_STR "CROS_FWID_MISSING"

struct image_data {
	uint32_t cookie1;
	char version[32];
	uint32_t size;
	int32_t rollback_version;
	uint32_t cookie2;

	/*
	 * Used by legacy EC code to make it easier to correlate EC and AP
	 * versions, deprecated and unused with zephyr.
	 */
	char cros_fwid[32];
	uint32_t cookie3;
} __packed;
/*
 * The offset is used in zephyr/include/cros/integrated_fwid.dtsi.
 * Be mindful of the FMAP's RO state when changing the offset.
 */
BUILD_ASSERT(offsetof(struct image_data, version) == 4);

extern const struct image_data current_image_data;
extern const char build_info[];
extern const char __image_data_offset[];
extern const void *__image_size;

/**
 * Get the number of commits of an image
 *
 * @return Number of commits in integer or 0 on error
 */
int ver_get_num_commits(enum ec_image copy);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_VERSION_H */
