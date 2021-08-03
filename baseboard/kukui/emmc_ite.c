/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "endian.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "intc.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#include "bootblock_data.h"

#define CPRINTS(format, args...) cprints(CC_SPI, format, ## args)

enum emmc_cmd {
	EMMC_ERROR = -1,
	EMMC_IDLE = 0,
	EMMC_PRE_IDLE,
	EMMC_BOOT,
};

static void emmc_reset_spi_tx(void)
{
	/* Reset TX FIFO and count monitor */
	IT83XX_SPI_TXFCR = IT83XX_SPI_TXFR | IT83XX_SPI_TXFCMR;
	/* Send idle state (high/0xff) if master clocks in data. */
	IT83XX_SPI_FCR = 0;
}

static void emmc_reset_spi_rx(void)
{
	/* End Rx FIFO access */
	IT83XX_SPI_TXRXFAR = 0;
	/* Reset RX FIFO and count monitor */
	IT83XX_SPI_FCR = IT83XX_SPI_RXFR | IT83XX_SPI_RXFCMR;
}

/*
 * Set SPI module work as eMMC Alternative Boot Mode.
 * (CS# pin isn't required, and dropping data until CMD goes low)
 */
static void emmc_enable_spi(void)
{
	/* Set SPI pin mux to eMMC (GPM2:CLK, GPM3:CMD, GPM6:DATA0) */
	IT83XX_GCTRL_PIN_MUX0 |= BIT(7);
	/* Enable eMMC Alternative Boot Mode */
	IT83XX_SPI_EMMCBMR |= IT83XX_SPI_EMMCABM;
	/* Reset TX and RX FIFO */
	emmc_reset_spi_tx();
	emmc_reset_spi_rx();
	/* Response idle state (high) */
	IT83XX_SPI_SPISRDR = 0xff;
	/* FIFO will be overwritten once it's full */
	IT83XX_SPI_GCR2 = 0;
	/* Write to clear pending interrupt bits */
	IT83XX_SPI_ISR = 0xff;
	IT83XX_SPI_RX_VLISR = IT83XX_SPI_RVLI;
	/* Enable RX fifo full interrupt */
	IT83XX_SPI_IMR = 0xff;
	IT83XX_SPI_RX_VLISMR |= IT83XX_SPI_RVLIM;
	IT83XX_SPI_IMR &= ~IT83XX_SPI_RX_FIFO_FULL;
	/*
	 * Enable interrupt to detect AP's BOOTBLOCK_EN_L. So EC is able to
	 * switch SPI module back to communication mode once BOOTBLOCK_EN_L
	 * goes high (AP Jumped to bootloader).
	 */
	gpio_clear_pending_interrupt(GPIO_BOOTBLOCK_EN_L);
	gpio_enable_interrupt(GPIO_BOOTBLOCK_EN_L);

	disable_sleep(SLEEP_MASK_EMMC);
	CPRINTS("eMMC emulation enabled");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, emmc_enable_spi, HOOK_PRIO_FIRST);

static void emmc_init_spi(void)
{
	/* Enable alternate function */
	gpio_config_module(MODULE_SPI_FLASH, 1);
}
DECLARE_HOOK(HOOK_INIT, emmc_init_spi, HOOK_PRIO_INIT_SPI + 1);

static void emmc_send_data_over_spi(uint8_t *tx, int tx_size, int rst_tx)
{
	int i;

	/* Reset TX FIFO and count monitor */
	if (rst_tx)
		IT83XX_SPI_TXFCR = IT83XX_SPI_TXFR | IT83XX_SPI_TXFCMR;
	/* CPU access TX FIFO1 and FIFO2 */
	IT83XX_SPI_TXRXFAR = IT83XX_SPI_CPUTFA;

	/* Write response data to TX FIFO */
	for (i = 0; i < tx_size; i += 4)
		IT83XX_SPI_CPUWTFDB0 = *(uint32_t *)(tx + i);
	/*
	 * After writing data to TX FIFO is finished, this bit will
	 * be to indicate the SPI peripheral controller.
	 */
	IT83XX_SPI_TXFCR = IT83XX_SPI_TXFS;
	/* End CPU access TX FIFO */
	IT83XX_SPI_TXRXFAR = 0;
	/* SPI module access TX FIFO */
	IT83XX_SPI_FCR = IT83XX_SPI_SPISRTXF;
}

static void emmc_bootblock_transfer(void)
{
	int tx_size, sent = 0, remaining = sizeof(bootblock_raw_data);
	uint8_t *raw = (uint8_t *)bootblock_raw_data;
	const uint32_t timeout_us = 200;
	uint32_t start;

	/*
	 * HW will transmit the data of FIFO1 or FIFO2 in turn.
	 * So when a FIFO is empty, we need to fill the FIFO out
	 * immediately.
	 */
	emmc_send_data_over_spi(&raw[sent], 256, 1);
	sent += 256;

	while (sent < remaining) {
		/* Wait for FIFO1 or FIFO2 have been transmitted */
		start = __hw_clock_source_read();
		while (!(IT83XX_SPI_TXFSR & BIT(0)) &&
			(__hw_clock_source_read() - start < timeout_us))
			;
		/* Abort an ongoing transfer due to a command is received. */
		if (IT83XX_SPI_ISR & IT83XX_SPI_RX_FIFO_FULL)
			break;
		/* fill out next 128 bytes to FIFO1 or FIFO2 */
		tx_size = (remaining - sent) < 128 ? (remaining - sent) : 128;
		emmc_send_data_over_spi(&raw[sent], tx_size, 0);
		sent += tx_size;
	}
}

static enum emmc_cmd emmc_parse_command(int index, uint32_t *cmd0)
{
	int32_t shift0;
	uint32_t data[3];

	data[0] = htobe32(cmd0[index]);
	data[1] = htobe32(cmd0[index+1]);
	data[2] = htobe32(cmd0[index+2]);

	if ((data[0] & 0xff000000) != 0x40000000) {
		/* Figure out alignment (cmd starts with 01) */
		/* Number of leading ones. */
		shift0 = __builtin_clz(~data[0]);

		data[0] = (data[0] << shift0) | (data[1] >> (32-shift0));
		data[1] = (data[1] << shift0) | (data[2] >> (32-shift0));
	}

	if (data[0] == 0x40000000 && data[1] == 0x0095ffff) {
		/* 400000000095 GO_IDLE_STATE */
		CPRINTS("goIdle");
		return EMMC_IDLE;
	}

	if (data[0] == 0x40f0f0f0 && data[1] == 0xf0fdffff) {
		/* 40f0f0f0f0fd GO_PRE_IDLE_STATE */
		CPRINTS("goPreIdle");
		return EMMC_PRE_IDLE;
	}

	if (data[0] == 0x40ffffff && data[1] == 0xfae5ffff) {
		/* 40fffffffae5 BOOT_INITIATION */
		CPRINTS("bootInit");
		return EMMC_BOOT;
	}

	CPRINTS("eMMC error");
	return EMMC_ERROR;
}

void spi_emmc_cmd0_isr(uint32_t *cmd0_payload)
{
	enum emmc_cmd cmd;

	for (int i = 0; i < 8; i++) {
		if (cmd0_payload[i] == 0xffffffff)
			continue;

		cmd = emmc_parse_command(i, &cmd0_payload[i]);

		if (cmd == EMMC_IDLE || cmd == EMMC_PRE_IDLE) {
			/* Abort an ongoing transfer. */
			emmc_reset_spi_tx();
			break;
		}

		if (cmd == EMMC_BOOT) {
			emmc_bootblock_transfer();
			break;
		}
	}
}
