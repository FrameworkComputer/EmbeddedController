/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by vboot library */

#define _STUB_IMPLEMENTATION_
#include "console.h"
#include "shared_mem.h"
#include "util.h"
#include "utility.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

#if 0 /* change this to debug memory usage */
#define DPRINTF CPRINTF
#else
#define DPRINTF(...)
#endif


/****************************************************************************/

void *Memcpy(void *dest, const void *src, uint64_t n)
{
	return memcpy(dest, src, (size_t)n);
}

void *Memset(void *d, const uint8_t c, uint64_t n)
{
	return memset(d, c, n);
}

int Memcmp(const void *src1, const void *src2, size_t n)
{
	size_t i;
	const uint8_t *a = src1;
	const uint8_t *b = src2;
	for (i = 0; i < n; i++) {
		if (*a != *b)
			return (*a < *b) ? -1 : 1;
		a++;
		b++;
	}

	return 0;
}

/****************************************************************************/
/* The vboot_api library requires some dynamic memory, but we don't have
 * malloc/free. Instead we have one chunk of shared RAM that we can gain
 * exclusive access to for a time. We'll have to experiment with various
 * algorithms to see what works best. This algorithm allocates and
 * reuses blocks, but never actually frees anything until all memory has been
 * reclaimed. */

/* Since we only need this stuff to boot, don't waste run-time .bss */
static struct {
	int bucket_size;			/* total RAM available */
	uint8_t *out_base;			/* malloc from here */
	int out_count;				/* number of active mallocs */
	int out_size;				/* high-water mark */

	/* We have a limited number of active mallocs. We never free, but we
	 * do reuse slots. */
#define MAX_SLOTS 8
	struct {
		int in_use;			/* is this slot active? */
		void *ptr;			/* starts here */
		size_t size;			/* how big */
	} slots[MAX_SLOTS];
} *bucket;


void *VbExMalloc(size_t size)
{
	int i, j;
	void *ptr = 0;

	if (!bucket) {
		i = shared_mem_size();
		if (EC_SUCCESS != shared_mem_acquire(i, 1, (char **)&bucket)) {
			CPRINTF("FAILED at %s:%d\n", __FILE__, __LINE__);
			ASSERT(0);
		}
		Memset(bucket, 0, sizeof(*bucket));
		bucket->bucket_size = i;
		bucket->out_base = (uint8_t *)(bucket + 1);
		bucket->out_size = sizeof(*bucket);
		DPRINTF("grab the bucket: at 0x%x, size 0x%x\n",
			bucket, bucket->bucket_size);
	}

	if (size % 8) {
		size_t tmp = (size + 8) & ~0x7ULL;
		DPRINTF("  %d -> %d\n", size, tmp);
		ASSERT(tmp >= size);
		size = tmp;
	}

	for (i = 0; i < MAX_SLOTS; i++) {
		if (!bucket->slots[i].in_use) {

			/* Found an empty one, but reuse the same size if one
			 * already exists. */
			for (j = i; j < MAX_SLOTS; j++) {
				if (!bucket->slots[j].in_use &&
				    size == bucket->slots[j].size) {
					/* empty AND same size */
					bucket->slots[j].in_use = 1;
					ptr = bucket->slots[j].ptr;
					DPRINTF("  = %d (%d)\n", j, size);
					goto out;
				}
			}

			/* no exact matches, must allocate a new chunk */
			ptr = bucket->out_base + bucket->out_size;
			bucket->out_size += size;

			bucket->slots[i].in_use = 1;
			bucket->slots[i].ptr = ptr;
			bucket->slots[i].size = size;
			DPRINTF("  + %d (%d)\n", i, size);
			goto out;
		}
	}

	CPRINTF("FAILED: no empty slots (%d/%d)\n", i, MAX_SLOTS);
	ASSERT(0);

out:
	bucket->out_count++;
	if (bucket->out_size >= bucket->bucket_size) {
		CPRINTF("FAILED: out of memory (%d/%d)\n",
			bucket->out_size, bucket->bucket_size);
		ASSERT(0);
	}

	return ptr;
}


void VbExFree(void *ptr)
{
	int i;

	for (i = 0; i < MAX_SLOTS; i++) {
		if (ptr == bucket->slots[i].ptr) {
			bucket->slots[i].in_use = 0;
			DPRINTF("  - %d (%d)\n", i, bucket->slots[i].size);
			break;
		}
	}
	if (MAX_SLOTS == i) {
		CPRINTF("FAILED: can't find ptr %x!\n", ptr);
		ASSERT(0);
	}

	bucket->out_count--;
	if (!bucket->out_count) {
		DPRINTF("dump the bucket: max used = %d\n", bucket->out_size);
		shared_mem_release(bucket);
		bucket = 0;
	}
}
