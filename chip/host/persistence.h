/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistence module for emulator */

#ifndef __CROS_EC_PERSISTENCE_H
#define __CROS_EC_PERSISTENCE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE *get_persistent_storage(const char *tag, const char *mode);

void release_persistent_storage(FILE *ps);

void remove_persistent_storage(const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PERSISTENCE_H */
