/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* sysjump implementation-specific structures */

#ifndef __CROS_EC_SYSJUMP_IMPL_H
#define __CROS_EC_SYSJUMP_IMPL_H

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Data passed between the current image and the next one when jumping between
 * images.
 */

#define JUMP_DATA_MAGIC 0x706d754a /* "Jump" */
#define JUMP_DATA_VERSION 3
#define JUMP_DATA_SIZE_V1 12 /* Size of version 1 jump data struct */
#define JUMP_DATA_SIZE_V2 16 /* Size of version 2 jump data struct */

#define JUMP_TAG_MAX_SIZE 255

#if !defined(CONFIG_RAM_SIZE) || !(CONFIG_RAM_SIZE > 0)
/* Disable check by setting jump data min address to zero */
#define JUMP_DATA_MIN_ADDRESS 0
#else
#define JUMP_DATA_MIN_ADDRESS \
	(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - CONFIG_PRESERVED_END_OF_RAM_SIZE)
#endif

struct jump_data {
	/*
	 * Add new fields to the _start_ of the struct, since we copy it to the
	 * _end_ of RAM between images.  This way, the magic number will always
	 * be the last word in RAM regardless of how many fields are added.
	 */

	/* Fields from version 3 */
	uint8_t reserved0; /* (used in proto1 to signal recovery mode) */
	int struct_size; /* Size of struct jump_data */

	/* Fields from version 2 */
	int jump_tag_total; /* Total size of all jump tags */

	/* Fields from version 1 */
	uint32_t reset_flags; /* Reset flags from the previous boot */
	int version; /* Version (JUMP_DATA_VERSION) */
	int magic; /* Magic number (JUMP_DATA_MAGIC).  If this
		    * doesn't match at pre-init time, assume no valid
		    * data from the previous image.
		    */
};

/**
 * Returns a pointer to the jump data structure.
 */
struct jump_data *get_jump_data(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SYSJUMP_IMPL_H */
