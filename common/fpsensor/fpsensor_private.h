/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Internal header file for common/fpsensor directory */

#ifndef __CROS_EC_FPSENSOR_PRIVATE_H
#define __CROS_EC_FPSENSOR_PRIVATE_H

#include <stdint.h>

#define CPRINTF(format, args...) cprintf(CC_FP, format, ## args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ## args)

int validate_fp_buffer_offset(uint32_t buffer_size, uint32_t offset,
			      uint32_t size);

#endif /* __CROS_EC_FPSENSOR_PRIVATE_H */
