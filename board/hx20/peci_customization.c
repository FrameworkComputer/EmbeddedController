/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "peci.h"
#include "peci_customization.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)

int peci_Rd_Pkg_Config(uint8_t index, uint16_t parameter, int rlen, uint8_t *in)
{
	int rv;
    uint8_t out[PECI_RD_PKG_CONFIG_WRITE_LENGTH];

	struct peci_data peci = {
		.cmd_code = PECI_CMD_RD_PKG_CFG,
		.addr = PECI_TARGET_ADDRESS,
		.w_len = PECI_RD_PKG_CONFIG_WRITE_LENGTH,
		.r_len = rlen,
		.w_buf = out,
		.r_buf = in,
		.timeout_us = PECI_RD_PKG_CONFIG_TIMEOUT_US,
	};

    out[0] = 0x00;	/* host ID */
    out[1] = index;
    out[2] = (parameter & 0xff);
    out[3] = ((parameter >> 8) & 0xff);

	rv = peci_transaction(&peci);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

int peci_Wr_Pkg_Config(uint8_t index, uint16_t parameter, uint32_t data, int wlen)
{
	int rv;
	int clen;
	uint8_t in[PECI_WR_PKG_CONFIG_READ_LENGTH] = {0};
	uint8_t out[PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD] = {0};

	struct peci_data peci = {
		.cmd_code = PECI_CMD_WR_PKG_CFG,
		.addr = PECI_TARGET_ADDRESS,
		.w_len = PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD,
		.r_len = PECI_WR_PKG_CONFIG_READ_LENGTH,
		.w_buf = out,
		.r_buf = in,
		.timeout_us = PECI_WR_PKG_CONFIG_TIMEOUT_US,
	};

	out[0] = 0x00; /* host ID */
	out[1] = index;
    out[2] = (parameter & 0xff);
    out[3] = ((parameter >> 8) & 0xff);

	for (clen = 4; clen < wlen - 1; clen++) {
		out[clen] = ((data >> ((clen - 4) * 8)) & 0xFF);
	}

	rv = peci_transaction(&peci);
	if (rv)
		return rv;

	return EC_SUCCESS;
}