/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "flash_log.h"
#include "init_chip.h"
#include "registers.h"
#include "trng.h"

void init_trng(void)
{
#if (!(defined(CONFIG_CUSTOMIZED_RO) && defined(SECTION_IS_RO)))
	/*
	 * Most of the trng initialization requires high permissions. If RO has
	 * dropped the permission level, dont try to read or write these high
	 * permission registers because it will cause rolling reboots. RO
	 * should do the TRNG initialization before dropping the level.
	 */
	if (!runlevel_is_high())
		return;
#endif

	GWRITE(TRNG, POST_PROCESSING_CTRL,
		GC_TRNG_POST_PROCESSING_CTRL_SHUFFLE_BITS_MASK |
		GC_TRNG_POST_PROCESSING_CTRL_CHURN_MODE_MASK);
	GWRITE(TRNG, SLICE_MAX_UPPER_LIMIT, 1);
	GWRITE(TRNG, SLICE_MIN_LOWER_LIMIT, 0);
	GWRITE(TRNG, TIMEOUT_COUNTER, 0x7ff);
	GWRITE(TRNG, TIMEOUT_MAX_TRY_NUM, 4);
	GWRITE(TRNG, POWER_DOWN_B, 1);
	GWRITE(TRNG, GO_EVENT, 1);
}

uint32_t rand(void)
{
	while (GREAD(TRNG, EMPTY)) {
		if (GREAD_FIELD(TRNG, FSM_STATE, FSM_IDLE)) {
			/* TRNG timed out, restart */
			GWRITE(TRNG, STOP_WORK, 1);
#if !defined(SECTION_IS_RO) && defined(CONFIG_FLASH_LOG)
			flash_log_add_event(FE_LOG_TRNG_STALL, 0, NULL);
#endif
			GWRITE(TRNG, GO_EVENT, 1);
		}
	}
	return GREAD(TRNG, READ_DATA);
}

void rand_bytes(void *buffer, size_t len)
{
	int random_togo = 0;
	int buffer_index = 0;
	uint32_t random_value;
	uint8_t *buf = (uint8_t *) buffer;

	/*
	 * Retrieve random numbers in 4 byte quantities and pack as many bytes
	 * as needed into 'buffer'. If len is not divisible by 4, the
	 * remaining random bytes get dropped.
	 */
	while (buffer_index < len) {
		if (!random_togo) {
			random_value = rand();
			random_togo = sizeof(random_value);
		}
		buf[buffer_index++] = random_value >>
			((random_togo-- - 1) * 8);
	}
}

#if !defined(SECTION_IS_RO) && defined(TEST_TRNG)
#include "console.h"
#include "watchdog.h"

static uint32_t histogram[256];
static int command_rand(int argc, char **argv)
{
	int count = 1000; /* Default number of cycles. */
	struct pair {
		uint32_t value;
		uint32_t count;
	};
	struct pair min;
	struct pair max;

	if (argc == 2)
		count = strtoi(argv[1], NULL, 10);

	memset(histogram, 0, sizeof(histogram));
	ccprintf("Retrieving %d random words.\n", count);
	while (count-- > 0) {
		uint32_t rvalue;
		int size;

		rvalue = rand();
		for (size = 0; size < sizeof(rvalue); size++)
			histogram[((uint8_t *)&rvalue)[size]]++;

		if (!(count % 10000))
			watchdog_reload();
	}

	min.count = ~0;
	max.count = 0;
	for (count = 0; count < ARRAY_SIZE(histogram); count++) {
		if (histogram[count] > max.count) {
			max.count = histogram[count];
			max.value = count;
			continue;
		}
		if (histogram[count] >= min.count)
			continue;

		min.count = histogram[count];
		min.value = count;
	}

	ccprintf("min %d(%d), max %d(%d)", min.count, min.value,
		 max.count, max.value);

	for (count = 0; count < ARRAY_SIZE(histogram); count++) {
		if (!(count % 8)) {
			ccprintf("\n");
			cflush();
		}
		ccprintf(" %6d", histogram[count]);
	}
	ccprintf("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rand, command_rand, NULL, NULL);
#endif /* !defined(SECTION_IS_RO) && defined(TEST_TRNG) */

#ifdef CRYPTO_TEST_SETUP
#include "extension.h"
/*
 * This extension command is similar to TPM2_GetRandom, but made
 * available for CRYPTO_TEST = 1 which disables TPM
 * Command structure, shared out of band with the test driver running
 * on the host:
 *
 * field     |    size  |                  note
 * ===================================================================
 * text_len  |    2     | size of the text to process, big endian
 */
static enum vendor_cmd_rc trng_test(enum vendor_cmd_cc code, void *buf,
				    size_t input_size, size_t *response_size)
{
	uint16_t text_len;
	uint8_t *cmd;
	size_t response_room = *response_size;

	if (input_size != sizeof(text_len)) {
		*response_size = 0;
		return VENDOR_RC_BOGUS_ARGS;
	}
	cmd = buf;
	text_len = *cmd++;
	text_len = text_len * 256 + *cmd++;
	text_len = MIN(text_len, response_room);
	rand_bytes(buf, text_len);
	*response_size = text_len;
	return VENDOR_RC_SUCCESS;
}

DECLARE_VENDOR_COMMAND(VENDOR_CC_TRNG_TEST, trng_test);

#endif   /* CRYPTO_TEST_SETUP */
