/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* sysjump implementation-specific structures */

#ifndef __CROS_EC_SYSJUMP_IMPL_H
#define __CROS_EC_SYSJUMP_IMPL_H

#include <inttypes.h>

/*
 * Data passed between the current image and the next one when jumping between
 * images.
 */

#define JUMP_DATA_MAGIC 0x706d754a  /* "Jump" */
#define JUMP_DATA_VERSION 3
#define JUMP_DATA_SIZE_V1 12  /* Size of version 1 jump data struct */
#define JUMP_DATA_SIZE_V2 16  /* Size of version 2 jump data struct */

struct jump_data {
	/*
	 * Add new fields to the _start_ of the struct, since we copy it to the
	 * _end_ of RAM between images.  This way, the magic number will always
	 * be the last word in RAM regardless of how many fields are added.
	 */

	/* Fields from version 3 */
	uint8_t reserved0;    /* (used in proto1 to signal recovery mode) */
	int struct_size;      /* Size of struct jump_data */

	/* Fields from version 2 */
	int jump_tag_total;   /* Total size of all jump tags */

	/* Fields from version 1 */
	uint32_t reset_flags; /* Reset flags from the previous boot */
	int version;          /* Version (JUMP_DATA_VERSION) */
	int magic;            /* Magic number (JUMP_DATA_MAGIC).  If this
			       * doesn't match at pre-init time, assume no valid
			       * data from the previous image.
			       */
};

#endif  /* __CROS_EC_SYSJUMP_IMPL_H */
