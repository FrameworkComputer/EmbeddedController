/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Utility functions for Chrome EC */

#include "util.h"

int strlen(const char *s)
{
	int len = 0;

	while (*s++)
		len++;

	return len;
}


int isspace(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}


int isdigit(int c)
{
	return c >= '0' && c <= '9';
}


int isalpha(int c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}


int tolower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}


int strcasecmp(const char *s1, const char *s2)
{
	int diff;
	do {
		diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	} while (*(s1++) && *(s2++));
	return 0;
}


int atoi(const char *nptr)
{
	int result = 0;
	int neg = 0;
	char c = '\0';

	while ((c = *nptr++) && isspace(c)) {}

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


/* Like strtol(), but for integers */
int strtoi(const char *nptr, char **endptr, int base)
{
	int result = 0;
	int neg = 0;
	int c = '\0';

	if (endptr)
		*endptr = (char *)nptr;

	while((c = *nptr++) && isspace(c)) {}

	if (c == '0' && *nptr == 'x') {
		base = 16;
		c = nptr[1];
		nptr += 2;
	} else if (base == 0) {
		base = 10;
		if (c == '-') {
			neg = 1;
			c = *nptr++;
		}
	}

	while (c) {
		if (c >= '0' && c < '0' + MIN(base, 10))
			result = result * base + (c - '0');
		else if (c >= 'A' && c < 'A' + base - 10)
			result = result * base + (c - 'A' + 10);
		else if (c >= 'a' && c < 'a' + base - 10)
			result = result * base + (c - 'a' + 10);
		else
			break;

		if (endptr)
			*endptr = (char *)nptr;
		c = *nptr++;
	}

	return neg ? -result : result;
}


void *memcpy(void *dest, const void *src, int len)
{
	/* TODO: optimized version using LDM/STM would be much faster */
	char *d = (char *)dest;
	const char *s = (const char *)src;
	while (len > 0) {
		*(d++) = *(s++);
		len--;
	}
	return dest;
}


void *memset(void *dest, int c, int len)
{
	/* TODO: optimized version using STM would be much faster */
	char *d = (char *)dest;
	while (len > 0) {
		*(d++) = c;
		len--;
	}
	return dest;
}


/* Like strncpy(), but guarantees null termination */
char *strzcpy(char *dest, const char *src, int len)
{
	char *d = dest;
	while (len > 1 && *src) {
		*(d++) = *(src++);
		len--;
	}
	*d = '\0';
	return dest;
}
