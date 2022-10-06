/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_INCLUDE_TRNG_H
#define __EC_INCLUDE_TRNG_H

#include <common.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Initialize the true random number generator.
 *
 * Not supported by all platforms.
 **/
void trng_init(void);

/**
 * Shutdown the true random number generator.
 *
 * The opposite operation of trng_init(), disable the hardware resources
 * used by the TRNG to save power.
 *
 * Not supported by all platforms.
 **/
void trng_exit(void);

/**
 * Retrieve a 32 bit random value.
 *
 * Not supported on all platforms.
 **/
uint32_t trng_rand(void);

/**
 * Output len random bytes into buffer.
 *
 * Not supported on all platforms.
 **/
void trng_rand_bytes(void *buffer, size_t len);

#endif /* __EC_INCLUDE_TRNG_H */
