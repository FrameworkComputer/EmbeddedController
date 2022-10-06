/*
 * Copyright (c) 1997-2015, Wind River Systems, Inc.
 * Copyright (c) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ATOMIC_H_
#define ZEPHYR_INCLUDE_SYS_ATOMIC_H_

/*
 * EC adaptation
 * Remove unnecessary includes.
 */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EC adaptation
 * Remove redefinition of atomic types.
 */

/*
 * EC adaptation
 * Remove including basic atomic operations.
 */


/* Portable higher-level utilities: */

/**
 * @defgroup atomic_apis Atomic Services APIs
 * @ingroup kernel_apis
 * @{
 */

/**
 * @brief Initialize an atomic variable.
 *
 * This macro can be used to initialize an atomic variable. For example,
 * @code atomic_t my_var = ATOMIC_INIT(75); @endcode
 *
 * @param i Value to assign to atomic variable.
 */
#define ATOMIC_INIT(i) (i)

/**
 * @brief Initialize an atomic pointer variable.
 *
 * This macro can be used to initialize an atomic pointer variable. For
 * example,
 * @code atomic_ptr_t my_ptr = ATOMIC_PTR_INIT(&data); @endcode
 *
 * @param p Pointer value to assign to atomic pointer variable.
 */
#define ATOMIC_PTR_INIT(p) (p)

/**
 * @cond INTERNAL_HIDDEN
 */

#define ATOMIC_BITS (sizeof(atomic_val_t) * 8)
#define ATOMIC_MASK(bit) BIT((unsigned long)(bit) & (ATOMIC_BITS - 1U))
#define ATOMIC_ELEM(addr, bit) ((addr) + ((bit) / ATOMIC_BITS))

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @brief This macro computes the number of atomic variables necessary to
 * represent a bitmap with @a num_bits.
 *
 * @param num_bits Number of bits.
 */
#define ATOMIC_BITMAP_SIZE(num_bits) (1 + ((num_bits) - 1) / ATOMIC_BITS)

/**
 * @brief Define an array of atomic variables.
 *
 * This macro defines an array of atomic variables containing at least
 * @a num_bits bits.
 *
 * @note
 * If used from file scope, the bits of the array are initialized to zero;
 * if used from within a function, the bits are left uninitialized.
 *
 * @cond INTERNAL_HIDDEN
 * @note
 * This macro should be replicated in the PREDEFINED field of the documentation
 * Doxyfile.
 * @endcond
 *
 * @param name Name of array of atomic variables.
 * @param num_bits Number of bits needed.
 */
#define ATOMIC_DEFINE(name, num_bits) \
	atomic_t name[ATOMIC_BITMAP_SIZE(num_bits)]

/**
 * @brief Atomically test a bit.
 *
 * This routine tests whether bit number @a bit of @a target is set or not.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 *
 * @return true if the bit was set, false if it wasn't.
 */
static inline bool atomic_test_bit(const atomic_t *target, int bit)
{
	atomic_val_t val = atomic_get(ATOMIC_ELEM(target, bit));

	return (1 & (val >> (bit & (ATOMIC_BITS - 1)))) != 0;
}

/**
 * @brief Atomically test and clear a bit.
 *
 * Atomically clear bit number @a bit of @a target and return its old value.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 *
 * @return true if the bit was set, false if it wasn't.
 */
static inline bool atomic_test_and_clear_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = ATOMIC_MASK(bit);
	atomic_val_t old;

	old = atomic_and(ATOMIC_ELEM(target, bit), ~mask);

	return (old & mask) != 0;
}

/**
 * @brief Atomically set a bit.
 *
 * Atomically set bit number @a bit of @a target and return its old value.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 *
 * @return true if the bit was set, false if it wasn't.
 */
static inline bool atomic_test_and_set_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = ATOMIC_MASK(bit);
	atomic_val_t old;

	old = atomic_or(ATOMIC_ELEM(target, bit), mask);

	return (old & mask) != 0;
}

/**
 * @brief Atomically clear a bit.
 *
 * Atomically clear bit number @a bit of @a target.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 */
static inline void atomic_clear_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = ATOMIC_MASK(bit);

	(void)atomic_and(ATOMIC_ELEM(target, bit), ~mask);
}

/**
 * @brief Atomically set a bit.
 *
 * Atomically set bit number @a bit of @a target.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 */
static inline void atomic_set_bit(atomic_t *target, int bit)
{
	atomic_val_t mask = ATOMIC_MASK(bit);

	(void)atomic_or(ATOMIC_ELEM(target, bit), mask);
}

/**
 * @brief Atomically set a bit to a given value.
 *
 * Atomically set bit number @a bit of @a target to value @a val.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 * @param val true for 1, false for 0.
 */
static inline void atomic_set_bit_to(atomic_t *target, int bit, bool val)
{
	atomic_val_t mask = ATOMIC_MASK(bit);

	if (val) {
		(void)atomic_or(ATOMIC_ELEM(target, bit), mask);
	} else {
		(void)atomic_and(ATOMIC_ELEM(target, bit), ~mask);
	}
}

/**
 * @}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ATOMIC_H_ */
