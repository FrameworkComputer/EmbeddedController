/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "espi.h"
#include "peci.h"
#include "peci_customization.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

#define ESPI_OOB_SMB_SLAVE_SRC_ADDR_EC 0x0F
#define ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW 0x20
#define ESPI_OOB_PECI_CMD 0x01


int espi_oob_peci_transaction(struct peci_data *peci)
{
	uint8_t espiOobMsg[16];
	uint8_t aw_FCS_calc;
	uint8_t OobWrLen = peci->w_len + 4;

	espiOobMsg[0] = peci->addr;
	espiOobMsg[1] = peci->w_len + 1;
	espiOobMsg[2] = peci->r_len;
	espiOobMsg[3] = peci->cmd_code;
	espiOobMsg[4] = peci->w_buf[0];  /* Host ID & Retry[0] */
	espiOobMsg[5] = peci->w_buf[1];  /* Index */
	espiOobMsg[6] = peci->w_buf[2];  /* Parameter LSB */
	espiOobMsg[7] = peci->w_buf[3];  /* Parameter MSB */
	espiOobMsg[8] = peci->w_buf[4];  /* Data 0 */
	espiOobMsg[9] = peci->w_buf[5];  /* Data 1 */
	espiOobMsg[10] = peci->w_buf[6]; /* Data 2 */
	espiOobMsg[11] = peci->w_buf[7]; /* Data 3 */

	if (peci->cmd_code == PECI_CMD_WR_PKG_CFG) {
		/* PECI_CMD_WR_PKG_CFG needs to calculate AW FCS */
		aw_FCS_calc = calc_AWFCS(espiOobMsg, peci->w_len+3);
		espiOobMsg[12] = aw_FCS_calc;
	}


	return espi_oob_build_peci_command(ESPI_OOB_SMB_SLAVE_SRC_ADDR_EC,
		ESPI_OOB_SMB_SLAVE_DEST_ADDR_PMC_FW, ESPI_OOB_PECI_CMD,
		OobWrLen, espiOobMsg, peci->r_buf);
}
