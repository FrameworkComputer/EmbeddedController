/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Commands for dumping EC memory */

#ifndef __CROS_EC_HOST_COMMAND_MEMORY_DUMP_H
#define __CROS_EC_HOST_COMMAND_MEMORY_DUMP_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register a memory range to be included in the memory dump.
 *
 * Do not register memory that could potentially contain sensitive data.
 *
 * @returns EC_SUCCESS or EC_XXX on error
 */
int register_memory_dump(uint32_t address, uint32_t size);

/**
 * Clear previously registered memory dump.
 *
 * @returns EC_SUCCESS or EC_XXX on error
 */
int clear_memory_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_HOST_COMMAND_MEMORY_DUMP_H */
