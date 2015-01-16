/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ALS_H
#define __CROS_EC_ALS_H

#include "common.h"

/* Defined in board.h */
enum als_id;

/* Initialized in board.c */
struct als_t {
	const char const *name;
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

#endif  /* __CROS_EC_ALS_H */
