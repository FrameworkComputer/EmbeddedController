/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistence module for emulator */

#ifndef _PERSISTENCE_H
#define _PERSISTENCE_H

#include <stdio.h>

FILE *get_persistent_storage(const char *tag, const char *mode);

void release_persistent_storage(FILE *ps);

void remove_persistent_storage(const char *tag);

#endif /* _PERSISTENCE_H */
