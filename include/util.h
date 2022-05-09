/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility functions and macros */

#ifndef __CROS_EC_UTIL_H
#define __CROS_EC_UTIL_H

#include "common.h"
#include "compile_time_macros.h"
#include "panic.h"

#include "builtin/assert.h"         /* For ASSERT(). */
#include <stdbool.h>
#include <stddef.h>
#ifdef CONFIG_ZEPHYR
#include <zephyr/sys/util.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Standard macros / definitions */
#define GENERIC_MAX(x, y) ((x) > (y) ? (x) : (y))
#define GENERIC_MIN(x, y) ((x) < (y) ? (x) : (y))
#ifndef MAX
#define MAX(a, b)					\
	({						\
		__typeof__(a) temp_a = (a);		\
		__typeof__(b) temp_b = (b);		\
							\
		GENERIC_MAX(temp_a, temp_b);		\
	})
#endif
#ifndef MIN
#define MIN(a, b)					\
	({						\
		__typeof__(a) temp_a = (a);		\
		__typeof__(b) temp_b = (b);		\
							\
		GENERIC_MIN(temp_a, temp_b);		\
	})
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Returns true if string is not null and not empty */
#define IS_NONEMPTY_STRING(s) ((s) && (s)[0])

/**
 * Ensure that value `v` is between `min` and `max`.
 *
 * @param v The value of interest.
 * @param min The minimum allowed value for `v`.
 * @param max The maximum allowed value for `v`.
 * @return `v` if it is already between `min`/`max`, `min` if `v` was smaller
 * than `min`, `max` if `v` was bigger than `max`.
 */
#ifndef CONFIG_ZEPHYR
#define CLAMP(v, min, max) MIN(max, MAX(v, min))
#endif

/*
 * Convert a pointer to a base struct into a pointer to the struct that
 * contains the base struct.  This requires knowing where in the contained
 * struct the base struct resides, this is the member parameter to downcast.
 */
#define DOWNCAST(pointer, type, member)					\
	((type *)(((uint8_t *) pointer) - offsetof(type, member)))

/* True of x is a power of two */
#define POWER_OF_TWO(x) ((x) && !((x) & ((x) - 1)))

/* Macro to check if the value is in range */
#ifndef CONFIG_ZEPHYR
#define IN_RANGE(x, min, max) ((x) >= (min) && (x) <= (max))
#endif

/*
 * macros for integer division with various rounding variants
 * default integer division rounds down.
 */
#define DIV_ROUND_UP(x, y) (((x) + ((y) - 1)) / (y))
#define DIV_ROUND_NEAREST(x, y) (((x) + ((y) / 2)) / (y))

/*
 * Swap two variables (requires c99)
 *
 * Swapping composites (e.g. a+b, x++) doesn't make sense. So, <a> and <b>
 * can only be a variable (x) or a pointer reference (*x) without operator.
 */
#define swap(a, b) \
	do { \
		typeof(a) __t__; \
		__t__ = a; \
		a = b; \
		b = __t__; \
	} while (0)

#ifndef HIDE_EC_STDLIB

/* Standard library functions */
int atoi(const char *nptr);

#ifdef CONFIG_ZEPHYR
#include <ctype.h>
#include <string.h>
#else
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isupper(int c);
int isprint(int c);
int tolower(int c);

int memcmp(const void *s1, const void *s2, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memset(void *dest, int c, size_t len);
void *memmove(void *dest, const void *src, size_t len);
void *memchr(const void *buffer, int c, size_t n);

/**
 * Find the first occurrence of the substring <s2> in the string <s1>
 *
 * @param s1	String where <s2> is searched.
 * @param s2	Substring to be located in <s1>
 * @return	Pointer to the located substring or NULL if not found.
 */
char *strstr(const char *s1, const char *s2);

/**
 * Calculates the length of the initial segment of s which consists
 * entirely of bytes not in reject.
 */
size_t strcspn(const char *s, const char *reject);

size_t strlen(const char *s);
char *strncpy(char *dest, const char *src, size_t n);
int strncmp(const char *s1, const char *s2, size_t n);
#endif

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t size);
size_t strnlen(const char *s, size_t maxlen);

/* Like strtol(), but for integers. */
int strtoi(const char *nptr, char **endptr, int base);

unsigned long long int strtoull(const char *nptr, char **endptr, int base);

/* Like strncpy(), but guarantees null termination. */
char *strzcpy(char *dest, const char *src, int len);

/**
 * Parses a boolean option from a string.
 *
 * Strings that set *dest=0 and return 1 (all case-insensitive):
 *   "off"
 *   "dis*"
 *   "n*"
 *   "f*"
 *
 * Strings that set *dest=1 and return 1 (all case-insensitive):
 *   "on"
 *   "ena*"
 *   "y*"
 *   "t*"
 *
 * Other strings return 0 and leave *dest unchanged.
 */
int parse_bool(const char *s, int *dest);
#endif  /* !HIDE_EC_STDLIB */

/**
 * Constant time implementation of memcmp to avoid timing side channels.
 */
int safe_memcmp(const void *s1, const void *s2, size_t len);

/* 64-bit divide-and-modulo.  Does the equivalent of:
 *
 *   r = *n % d;
 *   *n /= d;
 *   return r;
 */
int uint64divmod(uint64_t *v, int by);

/**
 * Get-and-clear next bit from mask.
 *
 * Starts with most significant bit.
 *
 * @param mask Bitmask to extract next bit from. Must NOT be zero.
 * @return bit position (0..31)
 */
int get_next_bit(uint32_t *mask);

/**
 * Check if |buffer| is full of 0x00 or 0xff.
 *
 * This function runs in constant execution time and is not vulnerable to
 * timing attacks.
 *
 * @param buffer the buffer to check.
 * @param size the number of bytes to check.
 * @return true if |buffer| is full of 0x00 or 0xff, false otherwise.
 */
bool bytes_are_trivial(const uint8_t *buffer, size_t size);

/**
 * Checks if address is power-of-two aligned to specified alignment.
 *
 * @param addr  address
 * @param align power-of-two alignment
 * @return true if addr is aligned to align, false otherwise
 */
bool is_aligned(uint32_t addr, uint32_t align);

/**
 * Get the alignment of x; the number of trailing zero bits.
 *
 * x must not be zero, otherwise the result is undefined (and will panic
 * in debug builds).
 *
 * @return the number of consecutive zero bits in x starting from the lsb
 */
int alignment_log2(unsigned int x);

/**
 * Reverse's the byte-order of the provided buffer.
 */
void reverse(void *dest, size_t len);


/****************************************************************************/
/* Conditional stuff.
 *
 * We often need to watch for transitions between one state and another, so
 * that we can issue warnings or take action ONCE. This abstracts that "have I
 * already reacted to this" stuff into a single set of functions.
 *
 * For example:
 *
 *     cond_t c;
 *
 *     cond_init_false(&c);
 *
 *     while(1) {
 *         int val = read_some_gpio();
 *         cond_set(&c, val);
 *
 *         if (cond_went_true(&c))
 *             host_event(SOMETHING_HAPPENED);
 *     }
 *
 */
typedef uint8_t cond_t;

/* Initialize a conditional to a specific state. Do this first. */
void cond_init(cond_t *c, int boolean);
static inline void cond_init_false(cond_t *c) { cond_init(c, 0); }
static inline void cond_init_true(cond_t *c) { cond_init(c, 1); }

/* Set the current state. Do this as often as you like. */
void cond_set(cond_t *c, int boolean);
static inline void cond_set_false(cond_t *c) { cond_set(c, 0); }
static inline void cond_set_true(cond_t *c) { cond_set(c, 1); }

/* Get the current state. Do this as often as you like. */
int cond_is(cond_t *c, int boolean);
static inline int cond_is_false(cond_t *c) { return cond_is(c, 0); }
static inline int cond_is_true(cond_t *c) { return cond_is(c, 1); }

/* See if the state has transitioned. If it has, the corresponding function
 * will return true ONCE only, until it's changed back.
 */
int cond_went(cond_t *c, int boolean);
static inline int cond_went_false(cond_t *c) { return cond_went(c, 0); }
static inline int cond_went_true(cond_t *c) { return cond_went(c, 1); }

/****************************************************************************/
/* Console command parsing */

/* Parse command-line arguments given integer shift value to obtain
 * offset and size.
 */
int parse_offset_size(int argc, char **argv, int shift,
			     int *offset, int *size);

/**
 * Print binary in hex and ASCII
 *
 * Sample output of hexdump(image_data.version, 30):
 *
 *   6e 61 6d 69 5f 76 32 2e 30 2e 37 37 34 2d 63 66 |nami_v2.0.774-cf|
 *   34 62 64 33 34 38 30 00 00 00 00 00 00 00       |4bd3480.......  |
 *
 * @param data	Data to be dumped
 * @param len	Size of data
 */
void hexdump(const uint8_t *data, int len);

#ifdef CONFIG_ASSEMBLY_MULA32
/*
 * Compute (a*b)+c[+d], where a, b, c[, d] are 32-bit integers, and the result
 * is 64-bit long.
 */
uint64_t mula32(uint32_t a, uint32_t b, uint32_t c);
uint64_t mulaa32(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
#else
static inline uint64_t mula32(uint32_t a, uint32_t b, uint32_t c)
{
	uint64_t ret = a;

	ret *= b;
	ret += c;

	return ret;
}

static inline uint64_t mulaa32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	uint64_t ret = a;

	ret *= b;
	ret += c;
	ret += d;

	return ret;
}
#endif

/**
 * Set enable bit(s) in register and wait for ready bit(s)
 *
 * @param reg    Register to be get and set for enable and ready
 * @param enable Bit(s) to be enabled
 * @param ready  Bit(s) to be read for readiness
 */
void wait_for_ready(volatile uint32_t *reg, uint32_t enable, uint32_t ready);

/**
 * Convert the ternary bit array (each element is either 0, 1, or 2) to a
 * non-standard ternary number system where the first 2^n natural numbers are
 * represented as they would be in a binary system (without any Z digits) and
 * the following 3^n-2^n numbers use the remaining ternary representations in
 * the normal ternary system order (skipping the values that were already used
 * up).
 *
 * This function is useful for converting BOARD ID, which is initially used a
 * binary and later decided to switch to tri-state after some revisions have
 * already been built.
 *
 * Example: For nbits = 2 we get the following representation:
 *
 *   Number      X1     X0
 *     0          0      0
 *     1          0      1
 *     2          1      0
 *     3          1      1	// Start counting ternaries back at 0 after this
 *     4          0      2	// Skipping 00 and 01 which are already used up
 *     5          1      2	// Skipping 10 and 11 which are already used up
 *     6          2      0
 *     7          2      1
 *     8          2      2
 *
 * @param bits		Array of ternary bits (LSB first).
 * @param nbits		Total number of bits.
 * @return Number in the binary-first ternary number system.
 */
int binary_first_base3_from_bits(int *bits, int nbits);

/**
 * Convert the binary bit array to integer value.
 *
 * @param bits    array of integers with values of 0 and 1
 * @param nbits   number of bits to decode
 * @return        integer decoded from bits
 */
int binary_from_bits(int *bits, int nbits);

/**
 * Convert the ternary bit array to integer value.
 * This function is used to handle 'Z' state of gpio as value of '2'.
 *
 * @param bits    array of integers with values of 0, 1 or 2
 * @param nbits   number of bits to decode
 * @return        integer decoded from bits
 */
int ternary_from_bits(int *bits, int nbits);

#ifdef __cplusplus
}
#endif

#endif  /* __CROS_EC_UTIL_H */
