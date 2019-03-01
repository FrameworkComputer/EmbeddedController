/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stuff from tpm2 directory. */

#include "nvmem_test.h"

#include "console.h"
#include "nvmem.h"
#include "util.h"

#define NVMEM_CR50_SIZE 272

uint32_t s_evictNvStart;
uint32_t s_evictNvEnd;

/* Calculate size of TPM NVMEM. */
#define MOCK_NV_MEMORY_SIZE                                                    \
	(NVMEM_PARTITION_SIZE - sizeof(struct nvmem_tag) - NVMEM_CR50_SIZE)

uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {MOCK_NV_MEMORY_SIZE,
					      NVMEM_CR50_SIZE};

/*
 * Sizes of the reserved objects stored in the TPM NVMEM. Note that the second
 * last object is in fact a variable size field starting with 4 bytes of size
 * and then up to 512 bytes of actual index data. The array below assumes that
 * the full 512 bytes of the index space are used.
 */
const uint16_t res_sizes[] = {4,  2,  2,  2,  66,   66,	 66,  66, 66,  66,
			      34, 34, 34, 66, 66,   66,	 8,   4,  134, 28,
			      3,  4,  4,  4,  4,    4,	 2,   15, 2,   8,
			      4,  4,  4,  96, 2844, 424, 516, 8};

static uint16_t res_addrs[ARRAY_SIZE(res_sizes)];

BOOL NvEarlyStageFindHandle(TPM_HANDLE handle)
{
	size_t i;

	res_addrs[0] = 0;

	for (i = 1; i < ARRAY_SIZE(res_addrs); i++)
		res_addrs[i] = res_addrs[i - 1] + res_sizes[i - 1];

	s_evictNvStart = res_addrs[i - 1] + res_sizes[i - 1];

	s_evictNvEnd = MOCK_NV_MEMORY_SIZE;
	return 0;
}

void NvGetReserved(UINT32 index, NV_RESERVED_ITEM *ri)
{
	uint32_t index_size;

	if (index >= ARRAY_SIZE(res_sizes)) {
		ri->size = 0;
		return;
	}

	ri->offset = res_addrs[index];
	if (index != NV_RAM_INDEX_SPACE) {
		ri->size = res_sizes[index];
		return;
	}

	memcpy(&index_size, nvmem_cache_base(NVMEM_TPM) + ri->offset,
	       sizeof(index_size));

	if (index_size == ~0)
		/* Must be starting with empty flash memeory. */
		index_size = 0;

	ri->size = index_size + sizeof(index_size);
}

UINT16 UINT16_Marshal(UINT16 *source, BYTE **buffer, INT32 *size)
{
	uint16_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe16(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 UINT32_Marshal(UINT32 *source, BYTE **buffer, INT32 *size)
{
	uint32_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe32(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 UINT64_Marshal(UINT64 *source, BYTE **buffer, INT32 *size)
{
	uint64_t value;

	if (!size || (*size < sizeof(value)))
		return 0;

	value = htobe64(*source);

	memcpy(*buffer, &value, sizeof(value));
	*buffer += sizeof(value);
	*size -= sizeof(value);

	return sizeof(value);
}

UINT16 TPM2B_DIGEST_Marshal(TPM2B_DIGEST *source, BYTE **buffer, INT32 *size)
{
	UINT16 total_size;
	INT32 i;
	uint8_t *p;

	total_size = UINT16_Marshal(&source->t.size, buffer, size);
	p = *buffer;

	for (i = 0; (i < source->t.size) && *size; ++i) {
		*p++ = source->t.buffer[i];
		*size -= 1;
	}

	total_size += i;
	*buffer = p;

	return total_size;
}

uint16_t TPM2B_AUTH_Marshal(TPM2B_AUTH *source, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Marshal(source, buffer, size);
}

uint16_t TPM2B_NONCE_Marshal(TPM2B_AUTH *source, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Marshal(source, buffer, size);
}

TPM_RC UINT16_Unmarshal(UINT16 *target, BYTE **buffer, INT32 *size)
{
	uint16_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be16toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC UINT32_Unmarshal(UINT32 *target, BYTE **buffer, INT32 *size)
{
	uint32_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be32toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC UINT64_Unmarshal(UINT64 *target, BYTE **buffer, INT32 *size)
{
	uint64_t value;

	if (!size || *size < sizeof(value))
		return TPM_RC_INSUFFICIENT;

	memcpy(&value, *buffer, sizeof(value));
	*target = be64toh(value);

	*buffer += sizeof(value);
	*size -= sizeof(value);

	return TPM_RC_SUCCESS;
}

TPM_RC TPM2B_DIGEST_Unmarshal(TPM2B_DIGEST *target, BYTE **buffer, INT32 *size)
{
	TPM_RC result;
	INT32 i;
	uint8_t *p;

	result = UINT16_Unmarshal(&target->t.size, buffer, size);

	if (result != TPM_RC_SUCCESS)
		return result;

	if (target->t.size == 0)
		return TPM_RC_SUCCESS;

	if ((target->t.size > sizeof(TPMU_HA)) || (target->t.size > *size))
		return TPM_RC_SIZE;

	p = *buffer;
	for (i = 0; i < target->t.size; ++i)
		target->t.buffer[i] = *p++;

	*buffer = p;
	*size -= i;

	return TPM_RC_SUCCESS;
}

TPM_RC TPM2B_AUTH_Unmarshal(TPM2B_AUTH *target, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Unmarshal(target, buffer, size);
}

TPM_RC TPM2B_NONCE_Unmarshal(TPM2B_AUTH *target, BYTE **buffer, INT32 *size)
{
	return TPM2B_DIGEST_Unmarshal(target, buffer, size);
}

#define ITER_INIT (~0)

static void *get_cache_addr(size_t offset)
{
	return (void *)(((uintptr_t)nvmem_cache_base(NVMEM_TPM)) + offset);
}

static void read_from_cache(size_t offset, size_t size, void *dest)
{
	nvmem_read(offset, size, dest, NVMEM_TPM);
}

static void write_to_cache(size_t offset, size_t size, void *src)
{
	nvmem_write(offset, size, src, NVMEM_TPM);
}

/* Copies of the appropriate functions from NV.c in TPM2 library. */
static uint32_t nv_next(uint32_t *iter)
{
	uint32_t currentIter;

	if (*iter == ITER_INIT)
		*iter = s_evictNvStart;

	if ((*iter + sizeof(uint32_t) > s_evictNvEnd) || !*iter)
		return 0;

	currentIter = *iter;
	read_from_cache(*iter, sizeof(uint32_t), iter);
	if (!*iter || (*iter == ITER_INIT))
		return 0;

	return currentIter + sizeof(uint32_t);
}

static uint32_t nv_get_end(void)
{
	uint32_t iter = ITER_INIT;
	uint32_t endAddr = s_evictNvStart;
	uint32_t currentAddr;

	while ((currentAddr = nv_next(&iter)) != 0)
		endAddr = currentAddr;

	if (endAddr != s_evictNvStart) {
		/* Read offset. */
		endAddr -= sizeof(uint32_t);
		read_from_cache(endAddr, sizeof(uint32_t), &endAddr);
	}
	return endAddr;
}

size_t add_evictable_obj(void *obj, size_t obj_size)
{
	uint32_t end_addr;
	uint32_t next_addr;
	uint32_t list_end = 0;

	end_addr = nv_get_end();

	next_addr = end_addr + sizeof(uint32_t) + obj_size;

	if (next_addr >= s_evictNvEnd) {
		ccprintf("%s: could not fit %d bytes!\n", __func__, obj_size);
		return 0;
	}

	/* Write next pointer */
	write_to_cache(end_addr, sizeof(uint32_t), &next_addr);
	/* Write entity data. */
	write_to_cache(end_addr + sizeof(uint32_t), obj_size, obj);

	/* Write the end of list if it fits. */
	if (next_addr + sizeof(uint32_t) <= s_evictNvEnd)
		write_to_cache(next_addr, sizeof(list_end), &list_end);

	return obj_size;
}

/*
 * It is the responsibility of the caller to pass the proper address of an
 * object in the cache.
 */
void drop_evictable_obj(void *obj)
{
	uint32_t next_addr;
	uint32_t list_end = 0;
	uint32_t obj_addr;

	obj_addr = (uintptr_t)obj - (uintptr_t)nvmem_cache_base(NVMEM_TPM);
	read_from_cache(obj_addr - sizeof(next_addr), sizeof(next_addr),
			&next_addr);
	ccprintf("%s:%d dropping obj at cache addr %x, offset %x, addr %p next "
		 "addr %x aka %x (off s_evictNvStart)\n",
		 __func__, __LINE__, obj_addr - s_evictNvStart, obj_addr, obj,
		 next_addr, next_addr - s_evictNvStart);

	/*
	 * Now, to make it easier to add objects behind the current one, let's
	 * pretend there is no more objects.
	 */
	write_to_cache(obj_addr - sizeof(next_addr), sizeof(list_end),
		       &list_end);

	if (!next_addr || (next_addr == s_evictNvEnd))
		return;

	/*
	 * Iterate over objects starting with next_addr, copying them into
	 * obj_addr.
	 */
	obj_addr = next_addr;
	while (1) {
		uint32_t next_next_addr;
		uint32_t next_obj_size;

		read_from_cache(next_addr, sizeof(next_next_addr),
				&next_next_addr);

		if (!next_next_addr || (next_next_addr == s_evictNvEnd))
			return;

		next_obj_size = next_next_addr - obj_addr - sizeof(uint32_t);
		add_evictable_obj(
			(void *)((uintptr_t)nvmem_cache_base(NVMEM_TPM) +
				 next_addr + sizeof(uint32_t)),
			next_obj_size);
		next_addr = next_next_addr;
		obj_addr += next_obj_size + sizeof(next_obj_size);
	}
}

void *evictable_offs_to_addr(uint16_t offset)
{
	return (void *)((uintptr_t)get_cache_addr(s_evictNvStart) + offset);
}
