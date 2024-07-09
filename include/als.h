/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ALS_H
#define __CROS_EC_ALS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Priority for ALS HOOK int */
#define HOOK_PRIO_ALS_INIT (HOOK_PRIO_DEFAULT + 1)

/* Defined in board.h */
enum als_id;

/* Initialized in board.c */
struct als_t {
	const char *const name;
	int (*init)(void);
	int (*read)(int *lux, int af);
	int attenuation_factor;
};

extern struct als_t als[];

/**
 * Read an ALS
 *
 * @param id		Which one?
 * @param lux	        Put value here
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int als_read(enum als_id id, int *lux);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ALS_H */
