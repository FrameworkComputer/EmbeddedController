/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This header is only needed for CR50 compatibility */

#ifndef __CROS_EC_STRINGS_H__
#define __CROS_EC_STRINGS_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void *s1, const void *s2, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t len);
void *memchr(const void *buffer, int c, size_t n);

size_t strnlen(const char *s, size_t maxlen);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_STRINGS_H__ */
