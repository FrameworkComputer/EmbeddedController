/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STRING_H__
#define __CROS_EC_STRING_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void *s1, const void *s2, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t len);
void *memchr(const void *buffer, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * Calculates the length of the initial segment of s which consists
 * entirely of bytes not in reject.
 */
size_t strcspn(const char *s, const char *reject);

/**
 * Find the first occurrence of the substring <s2> in the string <s1>
 *
 * @param s1	String where <s2> is searched.
 * @param s2	Substring to be located in <s1>
 * @return	Pointer to the located substring or NULL if not found.
 */
char *strstr(const char *s1, const char *s2);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_STRING_H__ */
