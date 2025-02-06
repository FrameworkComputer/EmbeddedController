/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Preserved ring buffers exists in noinit memory at the end of used ram. This
 * ring buffer is intended to be preserved across reboots and sysjumps.
 *
 * A rolling checksum ensures the buffer is uncorrupted. Corruption may be
 * caused by power loss, misalignment with RO ram, or conflicting writes.
 *
 * Preserved ring buffers are designed to be low overhead and fast, especially
 * for writes. This makes these ring buffers well suited for tracing and logging
 * use cases. Locking may be provided at a higher level. Verification is slow,
 * so it should only be performed during initialization and before dumping the
 * buffer.
 *
 * This ring buffer does not support dequeuing, it can only be cleared by
 * resetting the buffer. This buffer is intended to be dumped all at once.
 *
 * The ring buffer supports uint8, uint16 or uint32 word sizes. It does not
 * support mixing word types on the same instance.
 */

#ifndef __PRESERVED_RING_BUF_H
#define __PRESERVED_RING_BUF_H

#include "link_defs.h"

/* Static ring buffer properties */
typedef struct {
	/* Buffer capacity in words */
	const uint32_t capacity;
	/* User specified version. Update if higher level schema changes. */
	const uint32_t version;
	const uint32_t word_size;
} preserved_ring_buf_const_t;

/* Preserved ring buffer properties */
typedef struct {
	/* Rolling checksum */
	uint32_t checksum;
	/* Write head. Only increments. Modulo with capacity to get index. */
	uint32_t head;
	/* Capacity of buffer in words */
	uint32_t capacity;
	/* User specified version. Update if higher level schema changes. */
	uint32_t version;
	/* Word size in bytes */
	uint32_t word_size;
} preserved_ring_buf_properties_t;

typedef struct {
	const preserved_ring_buf_const_t constants;
	volatile preserved_ring_buf_properties_t *properties;
	union {
		volatile uint8_t *as_uint8;
		volatile uint16_t *as_uint16;
		volatile uint32_t *as_uint32;
	} buffer;
} preserved_ring_buf_t;

#define DECLARE_PRESERVED_RING_BUF(word_type, name, _capacity, _version)          \
	/* Buffer capacity must be a factor of 2 */                               \
	BUILD_ASSERT((_capacity & (_capacity - 1)) == 0);                         \
	/* Word size must be 1, 2, or 4 */                                        \
	BUILD_ASSERT((sizeof(word_type) == 1) || (sizeof(word_type) == 2) ||      \
		     (sizeof(word_type) == 4));                                   \
	static volatile uint8_t _preserved_ring_buf_##name##_buffer               \
		[_capacity * sizeof(word_type)] __uncached __aligned(4)           \
			__noinit_end_of_ram(                                      \
				_preserved_ring_buf_##name##_buffer);             \
	static volatile preserved_ring_buf_properties_t                           \
		_preserved_ring_buf_##name##_properties __uncached __aligned(     \
			4)                                                        \
			__noinit_end_of_ram(                                      \
				_preserved_ring_buf_##name##_properties);         \
	static const preserved_ring_buf_t _preserved_ring_buf_##name##_data = { \
			.buffer = { \
				.as_uint8 = _preserved_ring_buf_##name##_buffer, \
			}, \
			.properties = &_preserved_ring_buf_##name##_properties, \
			.constants = { \
				.capacity = _capacity, \
				.version = _version, \
				.word_size = sizeof(word_type), \
			}, \
    	}; \
	static const preserved_ring_buf_t *name =                                 \
		&_preserved_ring_buf_##name##_data
/*
 * Zero the buffer and set all preserved properties to the constant
 * values.
 */
void preserved_ring_buf_reset(const preserved_ring_buf_t *buf);

/*
 * Check if buffer is valid. Validation is expensive, it should only be
 * performed at initialization and before dumping.
 */
bool preserved_ring_buf_verify(const preserved_ring_buf_t *buf);

/*
 * Returns the length of the ring buffer.
 */
uint32_t preserved_ring_buf_len(const preserved_ring_buf_t *buf);

/*
 * Returns preserved user defined version
 */
uint32_t preserved_ring_buf_version(const preserved_ring_buf_t *buf);

/*
 * Returns preserved buffer capacity
 */
uint32_t preserved_ring_buf_capacity(const preserved_ring_buf_t *buf);

/*
 * Write a word to the buffer and increment head.
 */
void preserved_ring_buf_write(const preserved_ring_buf_t *buf,
			      const uint32_t word);

/*
 * Returns the word at the offset from the tail.
 */
uint32_t preserved_ring_buf_read(const preserved_ring_buf_t *buf,
				 uint32_t offset);

#endif /* __PRESERVED_RING_BUF_H */
