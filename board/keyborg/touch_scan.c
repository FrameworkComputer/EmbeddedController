/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Touch scanning module */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "debug.h"
#include "dma.h"
#include "encode.h"
#include "gpio.h"
#include "hooks.h"
#include "master_slave.h"
#include "registers.h"
#include "spi_comm.h"
#include "task.h"
#include "timer.h"
#include "touch_scan.h"
#include "util.h"

#define TS_PIN_TO_CR(p) ((((p).port_id + 1) << 16) | (1 << (p).pin))
#define TS_GPIO_TO_BASE(p) (0x40010800 + (p) * 0x400)

static uint8_t buf[2][ROW_COUNT * 2];

#ifdef CONFIG_KEYBORG_FAST_SCAN
#define SCAN_BUF_SIZE (DIV_ROUND_UP(COL_COUNT * 2, 32) + 2)
#define GET_SCAN_NEEDED(x) (scan_needed[(x) / 32 + 1] & (1 << ((x) & 31)))
static uint32_t scan_needed[SCAN_BUF_SIZE];
#else
#define GET_SCAN_NEEDED(x) 1
#endif

#define SPAN_LENGTH (2 * COL_SPAN + 1)
#define SPAN_MASK ((1 << SPAN_LENGTH) - 1)

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
	} else if (type == PIN_Z) {
		mask |= 0x44444444 & mode;
	} else if (type == PIN_ROW) {
		/* Nothing for PIN_ROW. Already analog input. */
	}

	REG32(addr) = mask;
}

void touch_scan_init(void)
{
	int i;

	for (i = 0; i < ROW_COUNT; ++i) {
		set_gpio(row_pins[i], PIN_ROW);
		STM32_PMSE_PxPMR(row_pins[i].port_id) |= 1 << row_pins[i].pin;
	}
	for (i = 0; i < COL_COUNT; ++i)
		set_gpio(col_pins[i], PIN_PD);

	for (i = 0; i < ROW_COUNT; ++i)
		mrcr_list[i] = TS_PIN_TO_CR(row_pins[i]);
	for (i = 0; i < COL_COUNT; ++i)
		mccr_list[i] = TS_PIN_TO_CR(col_pins[i]);
}

void touch_scan_enable_interrupt(void)
{
	int i;

	/* Set ALLROW and ALLCOL */
	for (i = 0; i < ROW_COUNT; ++i)
		set_gpio(row_pins[i], PIN_Z);
	for (i = 0; i < COL_COUNT; ++i) {
		set_gpio(col_pins[i], PIN_COL);
		STM32_PMSE_PxPMR(col_pins[i].port_id) |= 1 << col_pins[i].pin;
	}
	STM32_PMSE_MCCR = (1 << 31) | (0 << 20);
	STM32_PMSE_MRCR = 1 << 31;

	/* Enable external interrupt. EXTI3 on port E. Rising edge */
	STM32_EXTI_RTSR |= 1 << 3;
	STM32_AFIO_EXTICR(0) = (STM32_AFIO_EXTICR(0) & ~0xf000) | (4 << 12);
	STM32_EXTI_IMR |= 1 << 3;
	task_clear_pending_irq(STM32_IRQ_EXTI3);
	task_enable_irq(STM32_IRQ_EXTI3);
}

void touch_scan_disable_interrupt(void)
{
	int i;

	for (i = 0; i < ROW_COUNT; ++i)
		set_gpio(row_pins[i], PIN_ROW);
	for (i = 0; i < COL_COUNT; ++i) {
		set_gpio(col_pins[i], PIN_PD);
		STM32_PMSE_PxPMR(col_pins[i].port_id) &=
					~(1 << col_pins[i].pin);
	}
}

void touch_scan_interrupt(void)
{
	STM32_EXTI_PR = STM32_EXTI_PR;
}
DECLARE_IRQ(STM32_IRQ_EXTI3, touch_scan_interrupt, 1);

static void discharge(void)
{
	int i;

	/*
	 * The value 20 is deducted from experiments.
	 * Somehow this needs to be in reverse order
	 */
	for (i = 20; i >= 0; --i)
		STM32_PMSE_MRCR = mrcr_list[i];
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

static inline uint8_t flush_adc(int id)
{
	uint16_t v;

#if ADC_SMPL_CYCLE_2 < ADC_QUNTZ_CYCLE_2
	while (!(STM32_ADC_SR(id) & (1 << 1)))
		;
#endif

	v = STM32_ADC_DR(id) >> ADC_WINDOW_POS;
	if (v > 255)
		v = 255;
	return v;
}

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

#ifdef CONFIG_KEYBORG_FAST_SCAN
static inline void set_scan_needed(int col)
{
	uint8_t word = (col - COL_SPAN + 32) / 32;
	uint8_t bit = (col - COL_SPAN + 32) & 31;

	scan_needed[word] |= SPAN_MASK << bit;
	if (bit + SPAN_LENGTH > 32)
		scan_needed[word + 1] |= SPAN_MASK >> (32 - bit);
}

int fast_scan(uint32_t *data)
{
	int col;
	int chip_col = 0;

	memset(data, 0, SCAN_BUF_SIZE * 4);

	STM32_PMSE_MRCR = 1 << 31;
	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (master_slave_is_master())
			chip_col = (col >= COL_COUNT) ? col - COL_COUNT : -1;
		else
			chip_col = (col < COL_COUNT) ? col : -1;
		if (chip_col >= 0) {
			enable_col(chip_col, 1);
			STM32_PMSE_MCCR = mccr_list[chip_col];
		}

		if (master_slave_sync(5) != EC_SUCCESS)
			goto fast_scan_err;

		start_adc_sample(0, ADC_SMPL_CPU_CYCLE);
		while (!(STM32_ADC_SR(0) & (1 << 1)))
			;
		if (flush_adc(0) >= COL_THRESHOLD)
			set_scan_needed(col);

		if (master_slave_sync(5) != EC_SUCCESS)
			goto fast_scan_err;
		if (chip_col >= 0) {
			enable_col(chip_col, 0);
			STM32_PMSE_MCCR = 0;
		}
	}
	STM32_PMSE_MRCR = 0;

	/* Discharge the panel */
	discharge();

	return EC_SUCCESS;
fast_scan_err:
	enable_col(chip_col, 0);
	STM32_PMSE_MCCR = 0;
	STM32_PMSE_MRCR = 0;
	return EC_ERROR_UNKNOWN;
}
#else
#define fast_scan(x) EC_SUCCESS
#endif

void scan_column(uint8_t *data)
{
	int i;

	STM32_PMSE_MRCR = mrcr_list[0];
	start_adc_sample(0, ADC_SMPL_CPU_CYCLE);
	STM32_PMSE_MRCR = mrcr_list[1];
	start_adc_sample(1, ADC_SMPL_CPU_CYCLE);

	for (i = 2; i < ROW_COUNT; ++i) {
		data[i - 2] = flush_adc(i & 1);
		STM32_PMSE_MRCR = mrcr_list[i];
		start_adc_sample(i & 1, ADC_SMPL_CPU_CYCLE);
	}

	while (!(STM32_ADC_SR(ROW_COUNT & 1) & (1 << 1)))
		;
	data[ROW_COUNT - 2] = flush_adc(ROW_COUNT & 1);
	while (!(STM32_ADC_SR((ROW_COUNT & 1) ^ 1) & (1 << 1)))
		;
	data[ROW_COUNT - 1] = flush_adc((ROW_COUNT & 1) ^ 1);
}

void touch_scan_slave_start(void)
{
	int col = 0, i, v;
	struct spi_comm_packet *resp = (struct spi_comm_packet *)buf;

	if (fast_scan(scan_needed) != EC_SUCCESS)
		goto slave_err;

	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (col < COL_COUNT) {
			enable_col(col, 1);
			STM32_PMSE_MCCR = mccr_list[col];
		}

		if (master_slave_sync(20) != EC_SUCCESS)
			goto slave_err;

		if (GET_SCAN_NEEDED(col)) {
			scan_column(resp->data);

			/* Reverse the scanned data */
			for (i = 0; ROW_COUNT - 1 - i > i; ++i) {
				v = resp->data[i];
				resp->data[i] = resp->data[ROW_COUNT - 1 - i];
				resp->data[ROW_COUNT - 1 - i] = v;
			}
			resp->size = ROW_COUNT;
		} else {
			resp->size = 0;
		}

		resp->cmd_sts = EC_SUCCESS;

		/* Flush the last response */
		if (col != 0)
			if (spi_slave_send_response_flush() != EC_SUCCESS)
				goto slave_err;

		/* Start sending the response for the current column */
		if (spi_slave_send_response_async(resp) != EC_SUCCESS)
			goto slave_err;

		/* Disable the current column and discharge */
		if (col < COL_COUNT) {
			enable_col(col, 0);
			STM32_PMSE_MCCR = 0;
		}
		discharge();
	}
	spi_slave_send_response_flush();
	master_slave_sync(20);
	return;
slave_err:
	if (col < COL_COUNT)
		enable_col(col, 0);
	STM32_PMSE_MCCR = 0;
	spi_slave_send_response_flush();
}

int touch_scan_full_matrix(void)
{
	struct spi_comm_packet cmd;
	const struct spi_comm_packet *resp;
	int col = 0;
	timestamp_t st = get_time();
	uint8_t *dptr = NULL, *last_dptr = NULL;

	cmd.cmd_sts = TS_CMD_FULL_SCAN;
	cmd.size = 0;

	if (spi_master_send_command(&cmd))
		goto master_err;

	encode_reset();

	if (fast_scan(scan_needed) != EC_SUCCESS)
		goto master_err;

	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (col >= COL_COUNT) {
			enable_col(col - COL_COUNT, 1);
			STM32_PMSE_MCCR = mccr_list[col - COL_COUNT];
		}

		if (master_slave_sync(20) != EC_SUCCESS)
			goto master_err;

		last_dptr = dptr;
		dptr = buf[col & 1];

		if (GET_SCAN_NEEDED(col))
			scan_column(dptr + ROW_COUNT);
		else
			memset(dptr + ROW_COUNT, 0, ROW_COUNT);

		if (col > 0) {
			/* Flush the data from the slave for the last column */
			resp = spi_master_wait_response_done();
			if (resp == NULL)
				goto master_err;
			if (resp->size)
				memcpy(last_dptr, resp->data, ROW_COUNT);
			else
				memset(last_dptr, 0, ROW_COUNT);
			encode_add_column(last_dptr);
		}

		/* Start receiving data for the current column */
		if (spi_master_wait_response_async() != EC_SUCCESS)
			goto master_err;

		/* Disable the current column and discharge */
		if (col >= COL_COUNT) {
			enable_col(col - COL_COUNT, 0);
			STM32_PMSE_MCCR = 0;
		}
		discharge();
	}

	resp = spi_master_wait_response_done();
	if (resp == NULL)
		goto master_err;
	if (resp->size)
		memcpy(last_dptr, resp->data, ROW_COUNT);
	else
		memset(last_dptr, 0, ROW_COUNT);
	encode_add_column(last_dptr);

	master_slave_sync(20);

	debug_printf("Sampling took %d us\n", get_time().val - st.val);
	encode_dump_matrix();

	return EC_SUCCESS;
master_err:
	spi_master_wait_response_done();
	if (col >= COL_COUNT)
		enable_col(col - COL_COUNT, 0);
	STM32_PMSE_MCCR = 0;
	return EC_ERROR_UNKNOWN;
}
