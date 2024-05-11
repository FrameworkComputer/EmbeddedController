/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_INCLUDE_TRNG_H
#define __EC_INCLUDE_TRNG_H

#include <stddef.h>
#include <stdint.h>

#include <common.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ZEPHYR
/*
 * Zephyr driver is responsible for initializing, enabling and disabling
 * hardware. In this case, trng_init() and trng_exit() does nothing.
 */
#define trng_init()
#define trng_exit()
#else
/**
 * Initialize the true random number generator.
 *
 * Not supported by all platforms.
 **/
void trng_init(void);

/**
 * Generate true random number.
 *
 * Not supported by all platforms.
 **/
uint32_t trng_rand(void);

/**
 * Shutdown the true random number generator.
 *
 * The opposite operation of trng_init(), disable the hardware resources
 * used by the TRNG to save power.
 *
 * Not supported by all platforms.
 **/
void trng_exit(void);
#endif /* CONFIG_ZEPHYR */

/**
 * Output len random bytes into buffer.
 *
 * Not supported on all platforms.
 **/
void trng_rand_bytes(void *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __EC_INCLUDE_TRNG_H */
