/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "preserved_ring_buf.h"

#ifdef SECTION_IS_RO
#error Preserved ring buffer should not be enabled in RO images.
#endif

#ifndef CONFIG_NOINIT_END_OF_RAM_SECTION
#error Preserved ring buffer depends on CONFIG_NOINIT_END_OF_RAM_SECTION
#endif

/*
 * On reset, the buffer is zero'd and all properties are set to the constant
 * values.
 */
void preserved_ring_buf_reset(const preserved_ring_buf_t *buf)
{
	buf->properties->checksum = 0;
	buf->properties->head = 0;
	buf->properties->version = buf->constants.version;
	buf->properties->capacity = buf->constants.capacity;
	buf->properties->word_size = buf->constants.word_size;
	memset((void *)buf->buffer.as_uint8, 0,
	       buf->constants.capacity * buf->constants.word_size);
}

uint32_t preserved_ring_buf_capacity(const preserved_ring_buf_t *buf)
{
	return buf->properties->capacity;
}

uint32_t preserved_ring_buf_version(const preserved_ring_buf_t *buf)
{
	return buf->properties->version;
}

/*
 * Checksum is across entire buffer, even if partially filled. This works
 * because the buffer is zero'd on reset. Checksum is performed on 4 byte
 * word boundaries to improve performance.
 */
static uint32_t
preserved_ring_buf_calc_checksum(const preserved_ring_buf_t *buf)
{
	uint32_t sum = 0;
	for (int i = 0; i < buf->constants.capacity; i++) {
		switch (buf->constants.word_size) {
		case 1:
			sum += buf->buffer.as_uint8[i];
			break;
		case 2:
			sum += buf->buffer.as_uint16[i];
			break;
		case 4:
			sum += buf->buffer.as_uint32[i];
			break;
		default:
			__builtin_unreachable();
		}
	}
	return sum;
}

/*
 * A buffer is valid if the preserved properties match the expected
 * constant properties and the checksum matches.
 */
bool preserved_ring_buf_verify(const preserved_ring_buf_t *buf)
{
	return (buf->properties->version == buf->constants.version &&
		buf->properties->capacity == buf->constants.capacity &&
		buf->properties->word_size == buf->constants.word_size &&
		buf->properties->checksum ==
			preserved_ring_buf_calc_checksum(buf));
}

/*
 * Returns the length of the ring buffer.
 * Before the buffer is filled, the length matches head.
 * After the buffer is filled, the length matches the buffer capacity.
 */
uint32_t preserved_ring_buf_len(const preserved_ring_buf_t *buf)
{
	return (buf->properties->head < buf->constants.capacity) ?
		       buf->properties->head :
		       buf->constants.capacity;
}

/*
 * Write word to buffer and increment head. Head is 32 bits and only increments.
 * Note: head will wrap at uint32 max. In this infrequent event the buffer will
 * effectively be truncated. The trade off is improved performance on write.
 *
 * Forcing always inline to optimize write speed.
 */
void inline __attribute__((always_inline))
preserved_ring_buf_write(const preserved_ring_buf_t *buf, uint32_t word)
{
	const uint32_t index =
		(buf->properties->head++ % buf->constants.capacity);
	switch (buf->constants.word_size) {
	case 1:
		buf->properties->checksum +=
			(word & 0xff) - buf->buffer.as_uint8[index];
		buf->buffer.as_uint8[index] = word;
		break;
	case 2:
		buf->properties->checksum +=
			(word & 0xffff) - buf->buffer.as_uint16[index];
		buf->buffer.as_uint16[index] = word;
		break;
	case 4:
		buf->properties->checksum +=
			word - buf->buffer.as_uint32[index];
		buf->buffer.as_uint32[index] = word;
		break;
	default:
		__builtin_unreachable();
	}
}

/*
 * Read a word at a given offset.
 * Offset is wrapped to buffer capacity.
 * Tail is 0 until head passes buffer capacity, then it matches head.
 */
uint32_t preserved_ring_buf_read(const preserved_ring_buf_t *buf,
				 const uint32_t offset)
{
	uint32_t tail = 0;
	if (buf->properties->head > buf->constants.capacity)
		tail = buf->properties->head;
	const uint32_t index = (tail + offset) % buf->constants.capacity;
	switch (buf->constants.word_size) {
	case 1:
		return buf->buffer.as_uint8[index];
	case 2:
		return buf->buffer.as_uint16[index];
	case 4:
		return buf->buffer.as_uint32[index];
	default:
		__builtin_unreachable();
	}
}
