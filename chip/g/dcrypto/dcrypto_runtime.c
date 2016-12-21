/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "internal.h"

#include "task.h"
#include "registers.h"

#define DMEM_NUM_WORDS 1024
#define IMEM_NUM_WORDS 1024

static task_id_t my_task_id;

static int dcrypto_is_initialized;

void dcrypto_init(void)
{
	int i;
	volatile uint32_t *ptr;

	if (dcrypto_is_initialized)
		return;

	/* Enable PMU. */
	REG_WRITE_MLV(GR_PMU_PERICLKSET0, GC_PMU_PERICLKSET0_DCRYPTO0_CLK_MASK,
		GC_PMU_PERICLKSET0_DCRYPTO0_CLK_LSB, 1);

	/* Reset. */
	REG_WRITE_MLV(GR_PMU_RST0, GC_PMU_RST0_DCRYPTO0_MASK,
		GC_PMU_RST0_DCRYPTO0_LSB, 0);

	/* Turn off random nops (for accurate measuring here). */
	/* TODO(ngm): enable for production. */
	GREG32(CRYPTO, RAND_STALL_CTL) = 0;

	/* Initialize DMEM. */
	ptr = GREG32_ADDR(CRYPTO, DMEM_DUMMY);
	for (i = 0; i < DMEM_NUM_WORDS; ++i)
		*ptr++ = 0xdddddddd;

	/* Initialize IMEM. */
	ptr = GREG32_ADDR(CRYPTO, IMEM_DUMMY);
	for (i = 0; i < IMEM_NUM_WORDS; ++i)
		*ptr++ = 0xdddddddd;

	GREG32(CRYPTO, INT_STATE) = -1;   /* Reset all the status bits. */
	GREG32(CRYPTO, INT_ENABLE) = -1;  /* Enable all status bits. */

	my_task_id = task_get_current();
	task_enable_irq(GC_IRQNUM_CRYPTO0_HOST_CMD_DONE_INT);

	/* Reset. */
	GREG32(CRYPTO, CONTROL) = 1;
	GREG32(CRYPTO, CONTROL) = 0;

	dcrypto_is_initialized = 1;
}

#define DCRYPTO_CALL_TIMEOUT_US  (700 * 1000)
#define TASK_EVENT_DCRYPTO_DONE  TASK_EVENT_CUSTOM(1)

uint32_t dcrypto_call(uint32_t adr)
{
	uint32_t event;

	do {
		/* Reset all the status bits. */
		GREG32(CRYPTO, INT_STATE) = -1;
	} while (GREG32(CRYPTO, INT_STATE) & 3);

	GREG32(CRYPTO, HOST_CMD) = 0x08000000 + adr; /* Call imem:adr. */

	event = task_wait_event_mask(TASK_EVENT_DCRYPTO_DONE,
				DCRYPTO_CALL_TIMEOUT_US);
	/* TODO(ngm): switch return value to an enum. */
	switch (event) {
	case TASK_EVENT_DCRYPTO_DONE:
		return 0;
	default:
		return 1;
	}
}

void __keep dcrypto_done_interrupt(void)
{
	GREG32(CRYPTO, INT_STATE) = GC_CRYPTO_INT_STATE_HOST_CMD_DONE_MASK;
	task_clear_pending_irq(GC_IRQNUM_CRYPTO0_HOST_CMD_DONE_INT);
	task_set_event(my_task_id, TASK_EVENT_DCRYPTO_DONE, 0);
}
DECLARE_IRQ(GC_IRQNUM_CRYPTO0_HOST_CMD_DONE_INT, dcrypto_done_interrupt, 1);

void dcrypto_imem_load(size_t offset, const uint32_t *opcodes,
			size_t n_opcodes)
{
	size_t i;
	volatile uint32_t *ptr = GREG32_ADDR(CRYPTO, IMEM_DUMMY);

	ptr += offset;
	/* Check first word and copy all only if different. */
	if (ptr[0] != opcodes[0]) {
		for (i = 0; i < n_opcodes; ++i)
			ptr[i] = opcodes[i];
	}
}

void dcrypto_dmem_load(size_t offset, const void *words, size_t n_words)
{
	size_t i;
	volatile uint32_t *ptr = GREG32_ADDR(CRYPTO, DMEM_DUMMY);
	const uint32_t *src = (const uint32_t *) words;
	struct access_helper *word_accessor = (struct access_helper *) src;

	ptr += offset * 8;  /* Offset is in 256 bit addresses. */
	for (i = 0; i < n_words; ++i) {
		/*
		 * The implementation of memcpy makes unaligned writes if src
		 * is unaligned. DMEM on the other hand requires writes to be
		 * aligned, so do a word-by-word copy manually here.
		 */
		ptr[i] = word_accessor[i].udata;
	}
}
