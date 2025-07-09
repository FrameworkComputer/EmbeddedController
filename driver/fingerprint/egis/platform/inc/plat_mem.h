/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocates a block of memory of a specified size and number of
 * elements.
 *
 * @param[in] count The number of elements to allocate.
 * @param[in] size The size of each element in bytes.
 *
 * @return Pointer to the allocated memory or NULL if the allocation failed.
 */
void *sys_alloc(size_t count, size_t size);

/**
 * @brief Releases a block of shared memory.
 *
 * @param[in] data A pointer to the memory block to be released.
 *
 */
void sys_free(void *data);

static inline void plat_free(void *x)
{
	sys_free(x);
}

// TODO (b/373435445): Combine PLAT_FREE and plat_free.
/**
 * @brief Deallocates memory and sets the provided pointer to NULL.
 *
 * @param[in] x A pointer to a pointer to the memory block to be freed.
 *
 */
static inline void PLAT_FREE(void **x)
{
	assert(x != NULL && *x != NULL);
	plat_free(*x);
	*x = NULL;
}

/**
 * @brief Allocates a block of memory of the specified size.
 *
 * @param[in] size The size of the memory block to allocate, in bytes.
 *
 * @return Pointer to the allocated memory or NULL if the allocation failed.
 */
static inline void *plat_alloc(size_t size)
{
	return sys_alloc(1, size);
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_MEM_H_ */
