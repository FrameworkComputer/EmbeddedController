/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Standard library utility functions for Chrome EC */

#include "common.h"
#include "console.h"
#include "printf.h"
#include "util.h"

#include <stdio.h>

/*
 * The following macros are defined in stdlib.h in the C standard library, which
 * conflict with the definitions in this file.
 */
#undef isspace
#undef isdigit
#undef isalpha
#undef isupper
#undef isprint
#undef tolower

/* Context for snprintf() */
struct snprintf_context {
	char *str;
	int size;
};

/**
 * Add a character to the string context.
 *
 * @param context	Context receiving character
 * @param c		Character to add
 * @return 0 if character added, 1 if character dropped because no space.
 */
static int snprintf_addchar(void *context, int c)
{
	struct snprintf_context *ctx = (struct snprintf_context *)context;

	if (!ctx->size)
		return 1;

	*(ctx->str++) = c;
	ctx->size--;
	return 0;
}

int crec_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	struct snprintf_context ctx;
	int rv;

	if (!str || !format || size <= 0)
		return -EC_ERROR_INVAL;

	ctx.str = str;
	ctx.size = size - 1; /* Reserve space for terminating '\0' */

	rv = vfnprintf(snprintf_addchar, &ctx, format, args);

	/* Terminate string */
	*ctx.str = '\0';

	return (rv == EC_SUCCESS) ? (ctx.str - str) : -rv;
}
#ifndef CONFIG_ZEPHYR
int vsnprintf(char *str, size_t size, const char *format, va_list args)
	__attribute__((weak, alias("crec_vsnprintf")));
#endif /* CONFIG_ZEPHYR */

int crec_snprintf(char *str, size_t size, const char *format, ...)
{
	va_list args;
	int rv;

	va_start(args, format);
	rv = crec_vsnprintf(str, size, format, args);
	va_end(args);

	return rv;
}
#ifndef CONFIG_ZEPHYR
int snprintf(char *str, size_t size, const char *format, ...)
	__attribute__((weak, alias("crec_snprintf")));
#endif /* CONFIG_ZEPHYR */

/*
 * TODO(b/237712836): Zephyr's libc should provide strcasecmp. For now we'll
 * use the EC implementation.
 */
__stdlib_compat int strcasecmp(const char *s1, const char *s2)
{
	int diff;

	do {
		diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	} while (*(s1++) && *(s2++));
	return 0;
}

/*
 * TODO(b/237712836): Remove this conditional once strcasecmp is added to
 * Zephyr's libc.
 */
#ifndef CONFIG_ZEPHYR
__stdlib_compat size_t strlen(const char *s)
{
	int len = 0;

	while (*s++)
		len++;

	return len;
}

__stdlib_compat size_t strnlen(const char *s, size_t maxlen)
{
	size_t len = 0;

	while (len < maxlen && *s) {
		s++;
		len++;
	}
	return len;
}

__stdlib_compat size_t strcspn(const char *s, const char *reject)
{
	size_t i;
	size_t reject_len = strlen(reject);

	for (i = 0; s[i] != 0; i++)
		for (size_t j = 0; j < reject_len; j++)
			if (s[i] == reject[j])
				return i;
	return i;
}

__stdlib_compat int isspace(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

__stdlib_compat int isdigit(int c)
{
	return c >= '0' && c <= '9';
}

__stdlib_compat int isalpha(int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

__stdlib_compat int isupper(int c)
{
	return c >= 'A' && c <= 'Z';
}

__stdlib_compat int isprint(int c)
{
	return c >= ' ' && c <= '~';
}

__stdlib_compat int tolower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

__stdlib_compat int strncasecmp(const char *s1, const char *s2, size_t size)
{
	int diff;

	if (!size)
		return 0;

	do {
		diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	} while (*(s1++) && *(s2++) && --size);
	return 0;
}

__stdlib_compat char *strstr(const char *s1, const char *s2)
{
	const char *p, *q, *r;
	size_t len1 = strlen(s1);
	size_t len2 = strlen(s2);

	if (len1 == 0 || len2 == 0 || len1 < len2)
		return NULL;

	r = s1 + len1 - len2 + 1;
	for (; s1 < r; s1++) {
		if (*s1 == *s2) {
			p = s1 + 1;
			q = s2 + 1;
			for (; q < s2 + len2;) {
				if (*p++ != *q++)
					break;
			}
			if (*q == '\0')
				return (char *)s1;
		}
	}
	return NULL;
}

__stdlib_compat unsigned long long int strtoull(const char *nptr, char **endptr,
						int base)
{
	uint64_t result = 0;
	int c = '\0';

	while ((c = *nptr++) && isspace(c))
		;

	if (c == '+') {
		c = *nptr++;
	} else if (c == '-') {
		if (endptr)
			*endptr = (char *)nptr - 1;
		return result;
	}

	base = find_base(base, &c, &nptr);

	while (c) {
		if (c >= '0' && c < '0' + MIN(base, 10))
			result = result * base + (c - '0');
		else if (c >= 'A' && c < 'A' + base - 10)
			result = result * base + (c - 'A' + 10);
		else if (c >= 'a' && c < 'a' + base - 10)
			result = result * base + (c - 'a' + 10);
		else
			break;

		c = *nptr++;
	}

	if (endptr)
		*endptr = (char *)nptr - 1;
	return result;
}
BUILD_ASSERT(sizeof(unsigned long long int) == sizeof(uint64_t));

__stdlib_compat int atoi(const char *nptr)
{
	int result = 0;
	int neg = 0;
	char c = '\0';

	while ((c = *nptr++) && isspace(c))
		;

	if (c == '-') {
		neg = 1;
		c = *nptr++;
	}

	while (isdigit(c)) {
		result = result * 10 + (c - '0');
		c = *nptr++;
	}

	return neg ? -result : result;
}

__keep __stdlib_compat int memcmp(const void *s1, const void *s2, size_t len)
{
	const char *sa = s1;
	const char *sb = s2;
	int diff = 0;

	while (len-- > 0) {
		diff = *(sa++) - *(sb++);
		if (diff)
			return diff;
	}

	return 0;
}

#if !(__has_feature(address_sanitizer) || __has_feature(memory_sanitizer))
__keep __stdlib_compat void *memcpy(void *dest, const void *src, size_t len)
{
	char *d = (char *)dest;
	const char *s = (const char *)src;
	uint32_t *dw;
	const uint32_t *sw;
	char *head;
	char *const tail = (char *)dest + len;
	/* Set 'body' to the last word boundary */
	uint32_t *const body = (uint32_t *)((uintptr_t)tail & ~3);

	if (((uintptr_t)dest & 3) != ((uintptr_t)src & 3)) {
		/* Misaligned. no body, no tail. */
		head = tail;
	} else {
		/* Aligned */
		if ((uintptr_t)tail < (((uintptr_t)d + 3) & ~3))
			/* len is shorter than the first word boundary */
			head = tail;
		else
			/* Set 'head' to the first word boundary */
			head = (char *)(((uintptr_t)d + 3) & ~3);
	}

	/* Copy head */
	while (d < head)
		*(d++) = *(s++);

	/* Copy body */
	dw = (uint32_t *)d;
	sw = (uint32_t *)s;
	while (dw < body)
		*(dw++) = *(sw++);

	/* Copy tail */
	d = (char *)dw;
	s = (const char *)sw;
	while (d < tail)
		*(d++) = *(s++);

	return dest;
}
#endif /* address_sanitizer || memory_sanitizer */

#if !(__has_feature(address_sanitizer) || __has_feature(memory_sanitizer))
__keep __stdlib_compat __visible void *memset(void *dest, int c, size_t len)
{
	char *d = (char *)dest;
	uint32_t cccc;
	uint32_t *dw;
	char *head;
	char *const tail = (char *)dest + len;
	/* Set 'body' to the last word boundary */
	uint32_t *const body = (uint32_t *)((uintptr_t)tail & ~3);

	c &= 0xff; /* Clear upper bits before ORing below */
	cccc = c | (c << 8) | (c << 16) | (c << 24);

	if ((uintptr_t)tail < (((uintptr_t)d + 3) & ~3))
		/* len is shorter than the first word boundary */
		head = tail;
	else
		/* Set 'head' to the first word boundary */
		head = (char *)(((uintptr_t)d + 3) & ~3);

	/* Copy head */
	while (d < head)
		*(d++) = c;

	/* Copy body */
	dw = (uint32_t *)d;
	while (dw < body)
		*(dw++) = cccc;

	/* Copy tail */
	d = (char *)dw;
	while (d < tail)
		*(d++) = c;

	return dest;
}
#endif /* address_sanitizer || memory_sanitizer */

#if !(__has_feature(address_sanitizer) || __has_feature(memory_sanitizer))
__keep __stdlib_compat void *memmove(void *dest, const void *src, size_t len)
{
	if ((uintptr_t)dest <= (uintptr_t)src ||
	    (uintptr_t)dest >= (uintptr_t)src + len) {
		/* Start of destination doesn't overlap source, so just use
		 * memcpy().
		 */
		return memcpy(dest, src, len);
	} else {
		/* Need to copy from tail because there is overlap. */
		char *d = (char *)dest + len;
		const char *s = (const char *)src + len;
		uint32_t *dw;
		const uint32_t *sw;
		char *head;
		char *const tail = (char *)dest;
		/* Set 'body' to the last word boundary */
		uint32_t *const body = (uint32_t *)(((uintptr_t)tail + 3) & ~3);

		if (((uintptr_t)dest & 3) != ((uintptr_t)src & 3)) {
			/* Misaligned. no body, no tail. */
			head = tail;
		} else {
			/* Aligned */
			if ((uintptr_t)tail > ((uintptr_t)d & ~3))
				/* Shorter than the first word boundary */
				head = tail;
			else
				/* Set 'head' to the first word boundary */
				head = (char *)((uintptr_t)d & ~3);
		}

		/* Copy head */
		while (d > head)
			*(--d) = *(--s);

		/* Copy body */
		dw = (uint32_t *)d;
		sw = (uint32_t *)s;
		while (dw > body)
			*(--dw) = *(--sw);

		/* Copy tail */
		d = (char *)dw;
		s = (const char *)sw;
		while (d > tail)
			*(--d) = *(--s);

		return dest;
	}
}
#endif /* address_sanitizer || memory_sanitizer */

__stdlib_compat void *memchr(const void *buffer, int c, size_t n)
{
	char *current = (char *)buffer;
	char *end = current + n;

	while (current != end) {
		if (*current == c)
			return current;
		current++;
	}
	return NULL;
}

__stdlib_compat char *strncpy(char *dest, const char *src, size_t n)
{
	char *d = dest;

	while (n && *src) {
		*d++ = *src++;
		n--;
	}
	if (n)
		*d = '\0';
	return dest;
}

__stdlib_compat int strncmp(const char *s1, const char *s2, size_t n)
{
	while (n--) {
		if (*s1 != *s2)
			return *s1 - *s2;
		if (!*s1)
			break;
		s1++;
		s2++;
	}
	return 0;
}
#endif /* !CONFIG_ZEPHYR */
