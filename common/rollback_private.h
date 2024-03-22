/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/** Internal header file for rollback.
 *
 * EC code should not normally include this. These are exposed so they can be
 * used by unit test code.
 */

#ifndef __CROS_EC_ROLLBACK_PRIVATE_H
#define __CROS_EC_ROLLBACK_PRIVATE_H

#include "config.h"

/*
 * Note: Do not change this structure without also updating
 * common/firmware_image.S .image.ROLLBACK section.
 */
struct rollback_data {
	int32_t id; /* Incrementing number to indicate which region to use. */
	int32_t rollback_min_version;
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
	uint8_t secret[CONFIG_ROLLBACK_SECRET_SIZE];
#endif
	/* cookie must always be last, as it validates the rest of the data. */
	uint32_t cookie;
};

int read_rollback(int region, struct rollback_data *data);

#ifdef CONFIG_ZTEST
#if !defined(CONFIG_BORINGSSL_CRYPTO) && !defined(CONFIG_LIBCRYPTOC)
void secure_clear(void *buffer, size_t size);
#endif
#endif

#endif /* __CROS_EC_ROLLBACK_PRIVATE_H */
