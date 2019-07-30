/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash_log.h"
#include "internal.h"
#include "registers.h"
#include "task.h"

#define DMEM_NUM_WORDS 1024
#define IMEM_NUM_WORDS 1024

static struct mutex dcrypto_mutex;
static volatile task_id_t my_task_id;
static uint8_t dcrypto_is_initialized;

static const uint32_t wiped_value = 0xdddddddd;

static void dcrypto_reset_and_wipe(void)
{
	int i;
	volatile uint32_t *ptr;

	/* Reset. */
	GREG32(CRYPTO, CONTROL) = GC_CRYPTO_CONTROL_RESET_MASK;
	GREG32(CRYPTO, CONTROL) = 0;

	/* Reset all the status bits. */
	GREG32(CRYPTO, INT_STATE) = -1;

	/* Wipe state. */
	GREG32(CRYPTO, WIPE_SECRETS) = 1;

	/* Wipe DMEM. */
	ptr = GREG32_ADDR(CRYPTO, DMEM_DUMMY);
	for (i = 0; i < DMEM_NUM_WORDS; ++i)
		*ptr++ = wiped_value;
}

static void dcrypto_wipe_imem(void)
{
	int i;
	volatile uint32_t *ptr;

	/* Wipe IMEM. */
	ptr = GREG32_ADDR(CRYPTO, IMEM_DUMMY);
	for (i = 0; i < IMEM_NUM_WORDS; ++i)
		*ptr++ = wiped_value;
}

void dcrypto_init_and_lock(void)
{
	mutex_lock(&dcrypto_mutex);
	my_task_id = task_get_current();

	if (dcrypto_is_initialized)
		return;

	/* Enable PMU. */
	REG_WRITE_MLV(GR_PMU_PERICLKSET0, GC_PMU_PERICLKSET0_DCRYPTO0_CLK_MASK,
		GC_PMU_PERICLKSET0_DCRYPTO0_CLK_LSB, 1);

	dcrypto_reset_and_wipe();
	dcrypto_wipe_imem();

	/* Turn off random nops (which are enabled by default). */
	GWRITE_FIELD(CRYPTO, RAND_STALL_CTL, STALL_EN, 0);
	/* Configure random nop percentage at 6%. */
	GWRITE_FIELD(CRYPTO, RAND_STALL_CTL, FREQ, 3);
	/* Now turn on random nops. */
	GWRITE_FIELD(CRYPTO, RAND_STALL_CTL, STALL_EN, 1);

	GREG32(CRYPTO, INT_STATE) = -1;   /* Reset all the status bits. */
	GREG32(CRYPTO, INT_ENABLE) = -1;  /* Enable all status bits. */

	task_enable_irq(GC_IRQNUM_CRYPTO0_HOST_CMD_DONE_INT);

	dcrypto_is_initialized = 1;
}

void dcrypto_unlock(void)
{
	mutex_unlock(&dcrypto_mutex);
}

#ifndef DCRYPTO_CALL_TIMEOUT_US
#define DCRYPTO_CALL_TIMEOUT_US  (700 * 1000)
#endif
/*
 * When running on Cr50 this event belongs in the TPM task event space. Make
 * sure there is no collision with events defined in ./common/tpm_registers.c.
 */
#define TASK_EVENT_DCRYPTO_DONE  TASK_EVENT_CUSTOM_BIT(0)

uint32_t dcrypto_call(uint32_t adr)
{
	uint32_t event;
	uint32_t state = 0;

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
		/*
		 * We expect only the CMD_RECV status bit to be set at this
		 * point. CMD_DONE got cleared in the interrupt handler. Any and
		 * all other bits are indicative of error.
		 * Except for MOD_OPERAND_OUT_OF_RANGE, which is noise.
		 */
		state = GREG32(CRYPTO, INT_STATE);
		if ((state &
		     ~(GC_CRYPTO_INT_STATE_MOD_OPERAND_OUT_OF_RANGE_MASK |
		       GC_CRYPTO_INT_STATE_HOST_CMD_RECV_MASK)) == 0)
			return 0;
		/* fall through */
	default:
		dcrypto_reset_and_wipe();
#ifdef CONFIG_FLASH_LOG
		/* State value of zero indicates event timeout. */
		flash_log_add_event(FE_LOG_DCRYPTO_FAILURE,
				    sizeof(state), &state);
#endif
		return 1;
	}
}

void __keep dcrypto_done_interrupt(void)
{
	GREG32(CRYPTO, INT_STATE) = GC_CRYPTO_INT_STATE_HOST_CMD_DONE_MASK;
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

uint32_t dcrypto_dmem_load(size_t offset, const void *words, size_t n_words)
{
	size_t i;
	volatile uint32_t *ptr = GREG32_ADDR(CRYPTO, DMEM_DUMMY);
	const uint32_t *src = (const uint32_t *) words;
	struct access_helper *word_accessor = (struct access_helper *) src;
	uint32_t diff = 0;

	ptr += offset * 8;  /* Offset is in 256 bit addresses. */
	for (i = 0; i < n_words; ++i) {
		/*
		 * The implementation of memcpy makes unaligned writes if src
		 * is unaligned. DMEM on the other hand requires writes to be
		 * aligned, so do a word-by-word copy manually here.
		 */
		uint32_t v = word_accessor[i].udata;

		diff |= (ptr[i] ^ v);
		ptr[i] = v;
	}
	return diff;
}

#ifdef DCRYPTO_RUNTIME_TEST
/*
 * Add console command "dcrypto_test" that runs a couple of engine failure
 * scenarios and checks for adequate handling thereof:
 * - error return code
 * - dmem erasure on error
 * - dmem preservation on success
 */
#include "console.h"

/* AUTO-GENERATED.  DO NOT MODIFY. */
/* clang-format off */
static const uint32_t IMEM_test_hang[] = {
/* @0x0: function forever[2] { */
#define CF_forever_adr 0
/*forever: */
	0x10080000, /* b forever */
	0x0c000000, /* ret */
/* } */
/* @0x2: function func17[2] { */
#define CF_func17_adr 2
	0x08000000, /* call &forever */
	0x0c000000, /* ret */
/* } */
/* @0x4: function func16[2] { */
#define CF_func16_adr 4
	0x08000002, /* call &func17 */
	0x0c000000, /* ret */
/* } */
/* @0x6: function func15[2] { */
#define CF_func15_adr 6
	0x08000004, /* call &func16 */
	0x0c000000, /* ret */
/* } */
/* @0x8: function func14[2] { */
#define CF_func14_adr 8
	0x08000006, /* call &func15 */
	0x0c000000, /* ret */
/* } */
/* @0xa: function func13[2] { */
#define CF_func13_adr 10
	0x08000008, /* call &func14 */
	0x0c000000, /* ret */
/* } */
/* @0xc: function func12[2] { */
#define CF_func12_adr 12
	0x0800000a, /* call &func13 */
	0x0c000000, /* ret */
/* } */
/* @0xe: function func11[2] { */
#define CF_func11_adr 14
	0x0800000c, /* call &func12 */
	0x0c000000, /* ret */
/* } */
/* @0x10: function func10[2] { */
#define CF_func10_adr 16
	0x0800000e, /* call &func11 */
	0x0c000000, /* ret */
/* } */
/* @0x12: function func9[2] { */
#define CF_func9_adr 18
	0x08000010, /* call &func10 */
	0x0c000000, /* ret */
/* } */
/* @0x14: function func8[2] { */
#define CF_func8_adr 20
	0x08000012, /* call &func9 */
	0x0c000000, /* ret */
/* } */
/* @0x16: function func7[2] { */
#define CF_func7_adr 22
	0x08000014, /* call &func8 */
	0x0c000000, /* ret */
/* } */
/* @0x18: function func6[2] { */
#define CF_func6_adr 24
	0x08000016, /* call &func7 */
	0x0c000000, /* ret */
/* } */
/* @0x1a: function func5[2] { */
#define CF_func5_adr 26
	0x08000018, /* call &func6 */
	0x0c000000, /* ret */
/* } */
/* @0x1c: function func4[2] { */
#define CF_func4_adr 28
	0x0800001a, /* call &func5 */
	0x0c000000, /* ret */
/* } */
/* @0x1e: function func3[2] { */
#define CF_func3_adr 30
	0x0800001c, /* call &func4 */
	0x0c000000, /* ret */
/* } */
/* @0x20: function func2[2] { */
#define CF_func2_adr 32
	0x0800001e, /* call &func3 */
	0x0c000000, /* ret */
/* } */
/* @0x22: function func1[2] { */
#define CF_func1_adr 34
	0x08000020, /* call &func2 */
	0x0c000000, /* ret */
/* } */
/* @0x24: function test[2] { */
#define CF_test_adr 36
	0x08000022, /* call &func1 */
	0x0c000000, /* ret */
/* } */
/* @0x26: function sigchk[2] { */
#define CF_sigchk_adr 38
	0xf8000004, /* sigini #4 */
	0xf9ccc3c2, /* sigchk #13419458 */
/* } */
};
/* clang-format on */

static int command_dcrypto_test(int argc, char *argv[])
{
	volatile uint32_t *ptr = GREG32_ADDR(CRYPTO, DMEM_DUMMY);
	uint32_t not_wiped = ~wiped_value;
	int result;

	dcrypto_init_and_lock();
	dcrypto_imem_load(0, IMEM_test_hang, ARRAY_SIZE(IMEM_test_hang));

	*ptr = not_wiped;
	result = dcrypto_call(CF_func2_adr); /* max legal stack, into hang */
	if (result != 1 || *ptr != wiped_value)
		ccprintf("dcrypto_test: fail1 %d,%08x\n", result, *ptr);

	*ptr = not_wiped;
	result = dcrypto_call(CF_test_adr); /* stack overflow */
	if (result != 1 || *ptr != wiped_value)
		ccprintf("dcrypto_test: fail2 %d,%08x\n", result, *ptr);

	*ptr = not_wiped;
	result = dcrypto_call(CF_sigchk_adr); /* cfi trap */
	if (result != 1 || *ptr != wiped_value)
		ccprintf("dcrypto_test: fail3 %d,%08x\n", result, *ptr);

	*ptr = not_wiped;
	result = dcrypto_call(CF_test_adr + 1); /* simple ret should succeed */
	if (result != 0 || *ptr != not_wiped)
		ccprintf("dcrypto_test: fail4 %d,%08x\n", result, *ptr);

	dcrypto_unlock();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(dcrypto_test, command_dcrypto_test, "",
			     "dcrypto test");

#endif /* DCRYPTO_RUNTIME_TEST */
