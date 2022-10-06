/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI interface for Chrome EC */
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "peci.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

#define PECI_MAX_RETRIES    3
#define PECI_MAX_FIFO_SIZE  32
#define WAIT_IDLE_TIMEOUT   60   /* ms */
#define POLYNOMIAL          0x07

/*completion codes*/
#define COMP_CODE       0
#define CC_PASS_MASK    (0 << 7)
#define CC_FAIL_MASK    (1 << 7)
#define CC_PASSED       0x40        /* command passed */
#define CC_TIMED_OUT    0x80        /* command timed-out */
#define CC_BAD          0x90        /* unknown-invalid-illegal request */
#define CPU_ADDR        0x30

/* PECI 3.0 does not support multiple originators (hosts) */
#define HOST_ID             0x00
#define RETRY_BIT           0x01

enum peci_status {
	PECI_STATUS_NO_ERR        = 0x00,
	PECI_STATUS_HOBY          = 0x01,
	PECI_STATUS_FINISH        = 0x02,
	PECI_STATUS_RD_FCS_ERR    = 0x04,
	PECI_STATUS_WR_FCS_ERR    = 0x08,
	PECI_STATUS_EXTERR        = 0x20,
	PECI_STATUS_BUSERR        = 0x40,
	PECI_STATUS_RCV_ERRCODE   = 0x80,
	PECI_STATUS_ERR_NEED_RST  = (PECI_STATUS_BUSERR | PECI_STATUS_EXTERR),
	PECI_STATUS_ANY_ERR       = (PECI_STATUS_RCV_ERRCODE |
					PECI_STATUS_BUSERR |
					PECI_STATUS_EXTERR |
					PECI_STATUS_WR_FCS_ERR |
					PECI_STATUS_RD_FCS_ERR),
	PECI_STATUS_ANY_BIT       = 0xFE,
	PECI_STATUS_TIMEOUT       = 0xFF,
};

struct peci_params_t {
	uint8_t cmd_fifo[PECI_MAX_FIFO_SIZE];
	uint8_t data_fifo[PECI_MAX_FIFO_SIZE];
	uint8_t cmd_length;
	uint8_t read_length;
	/* check and display the completion code for command */
	uint8_t check_completion;
	uint8_t host_byte;
	uint8_t retry_valid;
	/* this parameter will determine how many of the returned data bytes */
	uint8_t read_bytes_to_display;
	uint8_t cmd_FCS;
};

/*****************************************************************************/
/* Internal functions */

/**
 * wait for IDLE ready (=1) by checking status register bit, output warning
 * message if TIMEOUT expires
 */
void wait_for_idle(void)
{
	uint16_t wait_timeout = WAIT_IDLE_TIMEOUT;

	while (((MCHP_PECI_STATUS2 & MCHP_PECI_STATUS2_IDLE) == 0) && wait_timeout--)
		udelay(1*MSEC);

	if (wait_timeout == 0)
		trace0(0, PECI, 0, "Timed-out waiting for IDLE=0");
}

/**
 * Issue PECI Reset and FIFO reset via CONTROL register, wait for IDLE ready
 * @param   mask
 */
void reset_PECI(uint8_t mask)
{
	if (mask & MCHP_PECI_CONTROL_RST)
		trace0(0, PECI, 0, "PECI Reset");

	if (mask & MCHP_PECI_CONTROL_FRST)
		trace0(0, PECI, 0, "PECI FIFO Reset");

	MCHP_PECI_CONTROL = mask;

	udelay(1*MSEC);

	MCHP_PECI_CONTROL = 0;
	wait_for_idle();

	MCHP_PECI_BAUD_CTRL = 1;
	MCHP_PECI_OPTIMAL_BIT_TIME_L = 0x16;
	MCHP_PECI_OPTIMAL_BIT_TIME_H = 0x00;
}

/**
 * write specified number of bytes to write buffer while FIFO is not full,
 * check idle for IDLE ready
 *
 * @param   cmd_fifo
 * @param   cmd_length
 */

void write_command(uint8_t cmd_fifo[], uint8_t cmd_length)
{
	uint8_t i = 0;
	uint16_t wait_timeout = WAIT_IDLE_TIMEOUT;

	while ((!(MCHP_PECI_STATUS2 & MCHP_PECI_STATUS2_WFF)) && (i < cmd_length))
		MCHP_PECI_WRITE_DATA = cmd_fifo[i++];

	wait_for_idle();

	MCHP_PECI_CONTROL |= MCHP_PECI_CONTROL_TXEN;

	while (((MCHP_PECI_STATUS1 & MCHP_PECI_STATUS1_EOF) == 0) && wait_timeout--)
		udelay(1*MSEC);


	if (wait_timeout == 0)
		trace0(0, PECI, 0, "Timed-out waiting for EOF");

	wait_for_idle();
}

/**
 * Read the specified number of bytes while FIFO not empty, return bytes in
 * read_data buffer
 *
 * @param   read_ptr
 * @param   num_bytes
 */
void read_databytes(uint8_t *read_ptr, uint8_t num_bytes)
{
	uint8_t readdcnt;

	for (readdcnt = 0; readdcnt < num_bytes; readdcnt++) {
		if (!(MCHP_PECI_STATUS2 & MCHP_PECI_STATUS2_RFE))
			*read_ptr = MCHP_PECI_READ_DATA;

		if (num_bytes > 1)
			read_ptr++;
	}
}

/**
 * do appropriate reset/retry after write/read of PECI commands
 *
 * @param   done
 * @param   zero_err
 */
void cleanup(uint8_t done, uint8_t zero_err)
{
	uint8_t errval = 0;

	errval = MCHP_PECI_ERROR;

	if (errval) {
		trace1(0, PECI, 0, "ERROR val=0x%02x - resetPECI", errval);
		MCHP_PECI_ERROR = errval;   /* RWC clear error by writing value back */
		reset_PECI(MCHP_PECI_CONTROL_RST | MCHP_PECI_CONTROL_FRST);
	} else {
		if (done) {
			trace0(0, PECI, 0, "Issue FIFO Reset to cleanup");
			reset_PECI(MCHP_PECI_CONTROL_FRST);
		} else if (zero_err) {
			trace0(0, PECI, 0, "retry command");
			reset_PECI(MCHP_PECI_CONTROL_RST | MCHP_PECI_CONTROL_FRST);
		} else
			trace0(0, PECI, 0, "retry command");
	}
}

/**
 * calculate the Assured Write value based on the number of bytes in input
 * buffer
 *
 * @param   data_blk_ptr
 * @param   length
 */
uint8_t calc_AWFCS(uint8_t *data_blk_ptr, unsigned int length)
{
	uint8_t crc = 0;
	uint8_t temp1, data_byte, bit0;
	unsigned int i, j;

	for (i = 0; i < length; i++) {
		data_byte = *data_blk_ptr++;

		for (j = 0; j < 8; j++) {
			bit0 = (data_byte & 0x80) ? 0x80 : 0;
			data_byte <<= 1;
			crc ^= bit0;
			temp1 = crc & 0x80;
			crc <<= 1;
			if (temp1)
				crc ^= POLYNOMIAL;
		}
	}
	crc ^= 0x80;
	return crc;
}

/**
 * main PECI function sends/recives PECI command bytes, check for FCS error,
 * retries commands based on parameters
 *
 * @param   PECI_PARAMS
 * @return  error status
 */
static uint8_t peci_trans(struct peci_params_t *peci)
{
	uint8_t error = 0;
	uint8_t done = 0, retry = 0, nzdata = 0, zero_error = 0, cc_error = 0;
	uint8_t i;

	peci->cmd_FCS = 0;

	while (!done) {
		if (!peci->retry_valid)
			done = 1;
		else {
			if (peci->host_byte && retry)
				*(peci->cmd_fifo + peci->host_byte) |= RETRY_BIT;
		}

		write_command(peci->cmd_fifo, peci->cmd_length);

		read_databytes(&peci->cmd_FCS, 1);

		if (MCHP_PECI_ERROR & MCHP_PECI_ERROR_FERR) {
			MCHP_PECI_ERROR = 0xFF;

			error++;
			trace0(0, PECI, 0, "Command FCS error!");
		} else {
			if (peci->read_length) {
				read_databytes(peci->data_fifo, (peci->read_length + 1));

				if ((!error) && (peci->cmd_FCS != 0)) {
					for (i = 0; i < (peci->read_length + 1); i++) {
						if (peci->data_fifo[i] != 0)
							nzdata++;
					}

					if (!nzdata) {
						trace0(0, PECI, 0, "Data error: All data = 0");
						error++;
						zero_error = 1;
					}
				}

				if (!error) {
					if (MCHP_PECI_ERROR & MCHP_PECI_ERROR_FERR) {
						MCHP_PECI_ERROR = 0xFF;

						error++;
						trace0(0, PECI, 0, "Data FCS error!");
					}

					if (!error) {
						if ((peci->check_completion) &&
						((peci->data_fifo[COMP_CODE] & CC_PASSED) != CC_PASSED)) {
							error++;
							cc_error++;

							if ((peci->data_fifo[COMP_CODE] & CC_BAD) == CC_BAD) {
								done = 1;
								cc_error = 2;
							}
						} else {
							done = 1;
							trace0(0, PECI, 0, "Command success");
						}
					}
				}
			} else {
				done = 1;
				trace0(0, PECI, 0, "Command success");
			}
		}

		if (error) {
			if (!done) {
				retry++;
				if (retry > PECI_MAX_RETRIES)
					done = 1;
			}

			if (done) {
				if (cc_error) {
					if (cc_error > 1)
						trace0(0, PECI, 0, "Illegal Request (no retry)");
					else {
						if (peci->data_fifo[COMP_CODE] == CC_TIMED_OUT)
							trace0(0, PECI, 0, "Timeout");
						else
							trace0(0, PECI, 0, "Others error");
					}
				}
				trace0(0, PECI, 0, "Command failed")
			}
		}

		cleanup(done, zero_error);

		if (!done)
			error = 0;

		zero_error = 0;
		cc_error = 0;
	}
	return error;

}

static void peci_init(void)
{
	/* Configure GPIOs */
	gpio_config_module(MODULE_PECI, 1);
}
DECLARE_HOOK(HOOK_INIT, peci_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/**
 * Start a PECI transaction
 *
 * @param peci transaction data
 *
 * @return zero if successful, non-zero if error
 */
int peci_transaction(struct peci_data *peci)
{
	struct peci_params_t peci_params;
	uint8_t rv;
	int index;
	int dlen;
	uint8_t aw_FCS_calc;

	peci_params.cmd_fifo[0] = peci->addr;
	peci_params.cmd_fifo[1] = peci->w_len+1;
	peci_params.cmd_fifo[2] = peci->r_len;
	peci_params.cmd_length = peci->w_len+4;
	peci_params.read_length = peci->r_len;
	peci_params.check_completion = 0;
	peci_params.retry_valid = 0;
	peci_params.host_byte = 0;

	if (peci->cmd_code != PECI_CMD_PING) {
		peci_params.cmd_fifo[3] = peci->cmd_code;

		/* GetDIB and GetTemp only command byte */
		if (!(peci->cmd_code == PECI_CMD_GET_DIB ||
			peci->cmd_code == PECI_CMD_GET_TEMP)) {

			for (index = 0; index < peci->w_len; index++)
				peci_params.cmd_fifo[index+4] = peci->w_buf[index];

			peci_params.check_completion = 1;
			peci_params.retry_valid = 1;
			peci_params.host_byte = 4;
		}
	}

	/* calculate the AW FCS value for 1 less byte */
	if (peci->cmd_code == PECI_CMD_WR_PKG_CFG) {
		aw_FCS_calc = calc_AWFCS(peci_params.cmd_fifo, peci_params.cmd_length - 1);
		peci_params.cmd_fifo[peci_params.cmd_length - 1] = aw_FCS_calc;
	}

	rv = peci_trans(&peci_params);

	/* TODO: check error message, if no error, pass the data fifo to *in*/
	for (dlen = 0; dlen < peci_params.read_length; dlen++)
		peci->r_buf[dlen] = peci_params.data_fifo[dlen];

	return rv;
}
