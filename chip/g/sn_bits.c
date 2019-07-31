/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id.h"
#include "board_space.h"
#include "console.h"
#include "extension.h"
#include "flash_info.h"
#include "util.h"
#include "wp.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

int read_sn_data(struct sn_data *sn)
{
	uint32_t *id_p;
	int i;

	/*
	 * SN Bits structure size is guaranteed to be divisible by 4, and it
	 * is guaranteed to be aligned at 4 bytes.
	 */

	id_p = (uint32_t *)sn;

	for (i = 0; i < sizeof(*sn); i += sizeof(uint32_t)) {
		int rv;

		rv = flash_physical_info_read_word
			(INFO_SN_DATA_OFFSET + i, id_p);
		if (rv != EC_SUCCESS) {
			CPRINTF("%s: failed to read word %d, error %d\n",
				__func__, i, rv);
			return rv;
		}
		id_p++;
	}
	return EC_SUCCESS;
}

static int write_sn_data(struct sn_data *sn_data, int header_only)
{
	int rv = EC_SUCCESS;

	/* Enable write access */
	flash_info_write_enable();

	/* Write sn bits */
	rv = flash_info_physical_write(INFO_SN_DATA_OFFSET,
				       header_only ?
				       SN_HEADER_SIZE : sizeof(*sn_data),
				       (const char *)sn_data);
	if (rv != EC_SUCCESS)
		CPRINTS("%s: write failed", __func__);

	/* Disable write access */
	flash_info_write_disable();

	return rv;
}

/**
 * Initialize SN data space in flash INFO1, and write sn hash. This can only
 * be called once per device; subsequent calls on a device that has already
 * had the sn hash written will fail.
 *
 * @param id	Pointer to a SN  structure to copy into INFO1
 *
 * @return EC_SUCCESS or an error code in cases of various failures to read or
 *		if the space has been already initialized.
 */
static int write_sn_hash(const uint32_t sn_hash[3])
{
	int rv = EC_ERROR_PARAM_COUNT;
	int i;
	struct sn_data sn_data;

	rv = read_sn_data(&sn_data);
	if (rv != EC_SUCCESS)
		return rv;

	/* Check the sn data space is currently uninitialized */
	for (i = 0; i < (sizeof(sn_data) / sizeof(uint32_t)); i++)
		if (((uint32_t *) &sn_data)[i] != 0xffffffff)
			return EC_ERROR_INVALID_CONFIG;

	sn_data.version = SN_DATA_VERSION;
	memcpy(sn_data.sn_hash, sn_hash, sizeof(sn_data.sn_hash));

	rv = write_sn_data(&sn_data, 0);

	return rv;
}

static int increment_rma_count(uint8_t inc)
{
	int rv = EC_ERROR_PARAM_COUNT;
	struct sn_data sn_data;

	rv = read_sn_data(&sn_data);
	if (rv != EC_SUCCESS)
		return rv;

	/* Make sure we know how to update this data */
	if (sn_data.version != SN_DATA_VERSION)
		return EC_ERROR_INVALID_CONFIG;

	/* Don't allow incrementing more than the number of bits */
	if (inc > RMA_COUNT_BITS)
		return EC_ERROR_INVAL;

	/*
	 * The RMA status is initially set to 0xff. We set bit 7
	 * to 0 to indicate the device has been RMA'd at least once,
	 * and use the remaining bits as a count of how many times
	 * the device has been RMA'd. The number of 0s represents
	 * the number of RMAs. As there are only 7 bits available
	 * for the count, a value of 0x00 means the device has
	 * been RMA'd at least 7 times (but we do not know how many).
	 *
	 * We allow incrementing by 0 or n (rather than 0 or 1) so
	 * that a device in any state can be put into the RMA'd with
	 * unknown count (0x00) state with a single call to this
	 * function.
	 */
	sn_data.rma_status <<= inc;
	sn_data.rma_status &= RMA_INDICATOR;

	rv = write_sn_data(&sn_data, 1);

	return rv;
}

static enum vendor_cmd_rc vc_sn_set_hash(enum vendor_cmd_cc code,
					 void *buf,
					 size_t input_size,
					 size_t *response_size)
{
	struct board_id bid;
	uint32_t sn_hash[3];
	uint8_t *pbuf = buf;

	*response_size = 1;

	if (input_size != sizeof(sn_hash)) {
		*pbuf = VENDOR_RC_BOGUS_ARGS;
		return VENDOR_RC_BOGUS_ARGS;
	}

	/*
	 * Only allow writing sn bits if we can successfully verify
	 * that the board ID has not been writen yet.
	 */
	if (read_board_id(&bid) != EC_SUCCESS ||
	    ~(bid.type & bid.type_inv & bid.flags) != 0) {
		*pbuf = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
	}

	memcpy(&sn_hash, pbuf, sizeof(sn_hash));

	/* We care about the LSB only. */
	*pbuf = (uint8_t) write_sn_hash(sn_hash);

	return *pbuf;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SN_SET_HASH, vc_sn_set_hash);

static enum vendor_cmd_rc vc_sn_inc_rma(enum vendor_cmd_cc code,
					void *buf,
					size_t input_size,
					size_t *response_size)
{
	uint8_t *pbuf = buf;

	if (wp_is_asserted())
		return EC_ERROR_ACCESS_DENIED;

	*response_size = 1;

	if (input_size != sizeof(*pbuf)) {
		*pbuf = VENDOR_RC_BOGUS_ARGS;
		return VENDOR_RC_BOGUS_ARGS;
	}

	/* We care about the LSB only. */
	*pbuf = (uint8_t) increment_rma_count(*pbuf);

	return *pbuf;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SN_INC_RMA, vc_sn_inc_rma);

static int command_sn(int argc, char **argv)
{
	int rv = EC_ERROR_PARAM_COUNT;
	struct sn_data sn;

	switch (argc) {
#ifdef CR50_DEV
	case 4:
	{
		char *e;

		sn.sn_hash[0] = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		sn.sn_hash[1] = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		sn.sn_hash[2] = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		rv = write_sn_hash(sn.sn_hash);
		if (rv != EC_SUCCESS)
			return rv;

		goto print_sn_data;
	}
	case 3:
	{
		int count;
		char *e;

		if (strcasecmp(argv[1], "rmainc") != 0)
			return EC_ERROR_PARAM1;

		count = strtoi(argv[2], &e, 0);
		if (*e || count > 7)
			return EC_ERROR_PARAM2;

		rv = increment_rma_count(count);
		if (rv != EC_SUCCESS)
			return rv;
	}
	/* fall through */
print_sn_data:
#endif
	case 1:
		rv = read_sn_data(&sn);
		if (rv == EC_SUCCESS)
			CPRINTF("Version: %02x\n"
				"RMA: %02x\n"
				"SN: %08x %08x %08x\n",
				sn.version, sn.rma_status,
				sn.sn_hash[0], sn.sn_hash[1], sn.sn_hash[2]);

		break;
	default:
		rv = EC_ERROR_PARAM_COUNT;
	}

	return rv;
}
DECLARE_SAFE_CONSOLE_COMMAND(sn,
			     command_sn, ""
#ifdef CR50_DEV
			     "[(sn0 sn1 sn2) | (rmainc n)]"
#endif
			     , "Get"
#ifdef CR50_DEV
			     "/Set"
#endif
			     " Serial Number Data");
