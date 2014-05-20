/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Touch scanning module */

#include "common.h"
#include "console.h"
#include "debug.h"
#include "dma.h"
#include "encode.h"
#include "gpio.h"
#include "hooks.h"
#include "master_slave.h"
#include "registers.h"
#include "spi_comm.h"
#include "timer.h"
#include "touch_scan.h"
#include "util.h"

#define TS_PIN_TO_CR(p) ((((p).port_id + 1) << 16) | (1 << (p).pin))
#define TS_GPIO_TO_BASE(p) (0x40010800 + (p) * 0x400)

static uint8_t buf[2][ROW_COUNT * 2];

static uint32_t mccr_list[COL_COUNT];
static uint32_t mrcr_list[ROW_COUNT];

static void set_gpio(const struct ts_pin pin, enum pin_type type)
{
	uint32_t addr, mode, mask;
	uint32_t port = TS_GPIO_TO_BASE(pin.port_id);
	uint32_t pmask = 1 << pin.pin;

	if (pmask & 0xff) {
		addr = port;
		mode = pmask;
	} else {
		addr = port + 0x04;
		mode = pmask >> 8;
	}
	mode = mode * mode * mode * mode * 0xf;

	mask = REG32(addr) & ~mode;

	if (type == PIN_COL) {
		/* Alternate output open-drain */
		mask |= 0xdddddddd & mode;
	} else if (type == PIN_PD) {
		mask |= 0x88888888 & mode;
		STM32_GPIO_BSRR(port) = pmask << 16;
	} else if (type == PIN_ROW) {
		/* Nothing for PIN_ROW. Already analog input. */
	}

	REG32(addr) = mask;
}

void touch_scan_init(void)
{
	int i;

	for (i = 0; i < ROW_COUNT; ++i) {
		set_gpio(col_pins[i], PIN_ROW);
		STM32_PMSE_PxPMR(row_pins[i].port_id) |= 1 << row_pins[i].pin;
	}
	for (i = 0; i < COL_COUNT; ++i)
		set_gpio(col_pins[i], PIN_PD);

	for (i = 0; i < ROW_COUNT; ++i)
		mrcr_list[i] = TS_PIN_TO_CR(row_pins[i]);
	for (i = 0; i < COL_COUNT; ++i)
		mccr_list[i] = TS_PIN_TO_CR(col_pins[i]);
}

static void start_adc_sample(int id, int wait_cycle)
{
	/* Clear EOC and STRT bit */
	STM32_ADC_SR(id) &= ~((1 << 1) | (1 << 4));

	/* Start conversion */
	STM32_ADC_CR2(id) |= (1 << 0);

	/* Wait for conversion start */
	while (!(STM32_ADC_SR(id) & (1 << 4)))
		;

	/* Each iteration takes 3 CPU cycles */
	asm("1: subs %0, #1\n"
	    "   bne 1b\n" :: "r"(wait_cycle / 3));
}

#if ADC_SMPL_CYCLE_2 < ADC_CONV_CYCLE_2 * 2
static uint16_t flush_adc(int id)
{
	while (!(STM32_ADC_SR(id) & (1 << 1)))
		;
	return STM32_ADC_DR(id) & ADC_READ_MAX;
}
#else
#define flush_adc(x) STM32_ADC_DR(x)
#endif

static void enable_col(int idx, int enabled)
{
	if (enabled) {
		set_gpio(col_pins[idx], PIN_COL);
		STM32_PMSE_PxPMR(col_pins[idx].port_id) |=
			1 << col_pins[idx].pin;
	} else {
		set_gpio(col_pins[idx], PIN_PD);
		STM32_PMSE_PxPMR(col_pins[idx].port_id) &=
			~(1 << col_pins[idx].pin);
	}
}

void scan_column(uint8_t *data)
{
	int i;

	STM32_PMSE_MRCR = mrcr_list[0];
	start_adc_sample(0, ADC_LONG_CPU_CYCLE);
	STM32_PMSE_MRCR = mrcr_list[1];
	start_adc_sample(1, ADC_LONG_CPU_CYCLE);

	for (i = 2; i < ROW_COUNT; ++i) {
		data[i - 2] = ADC_DATA_WINDOW(flush_adc(i & 1));
		STM32_PMSE_MRCR = mrcr_list[i];
		start_adc_sample(i & 1, ADC_SHORT_CPU_CYCLE);
	}

	while (!(STM32_ADC_SR(ROW_COUNT & 1) & (1 << 1)))
		;
	data[ROW_COUNT - 2] = ADC_DATA_WINDOW(flush_adc(ROW_COUNT & 1));
	while (!(STM32_ADC_SR((ROW_COUNT & 1) ^ 1) & (1 << 1)))
		;
	data[ROW_COUNT - 1] = ADC_DATA_WINDOW(flush_adc((ROW_COUNT & 1) ^ 1));
}

void touch_scan_slave_start(void)
{
	int col, i, v;
	struct spi_comm_packet *resp = (struct spi_comm_packet *)buf;

	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (col < COL_COUNT) {
			enable_col(col, 1);
			STM32_PMSE_MCCR = mccr_list[col];
		}

		if (master_slave_sync(20) != EC_SUCCESS)
			return;

		scan_column(resp->data);
		resp->cmd_sts = EC_SUCCESS;

		/* Reverse the scanned data */
		for (i = 0; ROW_COUNT - 1 - i > i; ++i) {
			v = resp->data[i];
			resp->data[i] = resp->data[ROW_COUNT - 1 - i];
			resp->data[ROW_COUNT - 1 - i] = v;
		}

		/* Trim trailing zeros. */
		for (i = 0; i < ROW_COUNT; ++i)
			if (resp->data[i] >= THRESHOLD)
				resp->size = i + 1;

		/* Flush the last response */
		if (col != 0)
			spi_slave_send_response_flush();

		if (master_slave_sync(20) != EC_SUCCESS)
			return;

		/* Start sending the response for the current column */
		spi_slave_send_response_async(resp);

		if (col < COL_COUNT) {
			enable_col(col, 0);
			STM32_PMSE_MCCR = 0;
		}
	}
	spi_slave_send_response_flush();
	master_slave_sync(20);
}

int touch_scan_full_matrix(void)
{
	struct spi_comm_packet cmd;
	const struct spi_comm_packet *resp;
	int col;
	timestamp_t st = get_time();
	uint8_t *dptr = NULL, *last_dptr = NULL;

	cmd.cmd_sts = TS_CMD_FULL_SCAN;
	cmd.size = 0;

	if (spi_master_send_command(&cmd))
		return EC_ERROR_UNKNOWN;

	encode_reset();
	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (col >= COL_COUNT) {
			enable_col(col - COL_COUNT, 1);
			STM32_PMSE_MCCR = mccr_list[col - COL_COUNT];
		}

		if (master_slave_sync(20) != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;

		last_dptr = dptr;
		dptr = buf[col & 1];

		scan_column(dptr + ROW_COUNT);

		if (col > 0) {
			/* Flush the data from the slave for the last column */
			resp = spi_master_wait_response_done();
			if (resp == NULL)
				return EC_ERROR_UNKNOWN;
			memcpy(last_dptr, resp->data, resp->size);
			memset(last_dptr + resp->size, 0,
					ROW_COUNT - resp->size);
			encode_add_column(last_dptr);
		}

		if (master_slave_sync(20) != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;

		/* Start receiving data for the current column */
		if (spi_master_wait_response_async() != EC_SUCCESS)
			return EC_ERROR_UNKNOWN;

		if (col >= COL_COUNT) {
			enable_col(col - COL_COUNT, 0);
			STM32_PMSE_MCCR = 0;
		}
	}

	resp = spi_master_wait_response_done();
	if (resp == NULL)
		return EC_ERROR_UNKNOWN;
	memcpy(last_dptr, resp->data, resp->size);
	memset(last_dptr + resp->size, 0, ROW_COUNT - resp->size);
	encode_add_column(last_dptr);

	master_slave_sync(20);

	debug_printf("Sampling took %d us\n", get_time().val - st.val);
	encode_dump_matrix();

	return EC_SUCCESS;
}
