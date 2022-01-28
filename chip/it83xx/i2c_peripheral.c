/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C module for Chrome EC */
#include "clock.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c_peripheral.h"
#include "registers.h"
#include <stddef.h>
#include <string.h>
#include "task.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* The size must be a power of 2 */
#define I2C_MAX_BUFFER_SIZE 0x100
#define I2C_SIZE_MASK (I2C_MAX_BUFFER_SIZE - 1)

#define I2C_READ_MAXFIFO_DATA 16
#define I2C_ENHANCED_CH_INTERVAL 0x80

/* Store controller to peripheral data of channel D, E, F by DMA */
static uint8_t in_data[I2C_ENHANCED_PORT_COUNT][I2C_MAX_BUFFER_SIZE]
			__attribute__((section(".h2ram.pool.i2cslv")));
/* Store peripheral to controller data of channel D, E, F by DMA */
static uint8_t out_data[I2C_ENHANCED_PORT_COUNT][I2C_MAX_BUFFER_SIZE]
			__attribute__((section(".h2ram.pool.i2cslv")));
/* Store read and write data of channel A by FIFO mode */
static uint8_t pbuffer[I2C_MAX_BUFFER_SIZE];

static uint32_t w_index;
static uint32_t r_index;
static int wr_done[I2C_ENHANCED_PORT_COUNT];

void buffer_index_reset(void)
{
	/* Reset write buffer index */
	w_index = 0;
	/* Reset read buffer index */
	r_index = 0;
}

/* Data structure to define I2C peripheral control configuration. */
struct i2c_periph_ctrl_t {
	int irq;              /* peripheral irq */
	/* offset from base 0x00F03500 register; -1 means unused. */
	int offset;
	enum clock_gate_offsets clock_gate;
	int dma_index;
};

/* I2C peripheral control */
const struct i2c_periph_ctrl_t i2c_periph_ctrl[] = {
	[IT83XX_I2C_CH_A] = {.irq = IT83XX_IRQ_SMB_A, .offset = -1,
		.clock_gate = CGC_OFFSET_SMBA, .dma_index = -1},
	[IT83XX_I2C_CH_D] = {.irq = IT83XX_IRQ_SMB_D, .offset = 0x180,
		.clock_gate = CGC_OFFSET_SMBD, .dma_index = 0},
	[IT83XX_I2C_CH_E] = {.irq = IT83XX_IRQ_SMB_E, .offset = 0x0,
		.clock_gate = CGC_OFFSET_SMBE, .dma_index = 1},
	[IT83XX_I2C_CH_F] = {.irq = IT83XX_IRQ_SMB_F, .offset = 0x80,
		.clock_gate = CGC_OFFSET_SMBF, .dma_index = 2},
};

void i2c_peripheral_read_write_data(int port)
{
	int periph_status, i;

	/* I2C peripheral channel A FIFO mode */
	if (port < I2C_STANDARD_PORT_COUNT) {
		int count;

		periph_status = IT83XX_SMB_SLSTA;

		/* bit0-4 : FIFO byte count */
		count = IT83XX_SMB_SFFSTA & 0x1F;

		/* Peripheral data register is waiting for read or write. */
		if (periph_status & IT83XX_SMB_SDS) {
			/* Controller to read data */
			if (periph_status & IT83XX_SMB_RCS) {
				for (i = 0; i < I2C_READ_MAXFIFO_DATA; i++)
					/* Return buffer data to controller */
					IT83XX_SMB_SLDA =
					pbuffer[(i + r_index) & I2C_SIZE_MASK];

				/* Index to next 16 bytes of read buffer */
				r_index += I2C_READ_MAXFIFO_DATA;
			}
			/* Controller to write data */
			else {
				/* FIFO Full */
				if (IT83XX_SMB_SFFSTA & IT83XX_SMB_SFFFULL) {
					for (i = 0; i < count; i++)
				/* Get data from controller to buffer */
						pbuffer[(w_index + i) &
					I2C_SIZE_MASK] = IT83XX_SMB_SLDA;
				}

				/* Index to next byte of write buffer */
				w_index += count;
			}
		}
		/* Stop condition, indicate stop condition detected. */
		if (periph_status & IT83XX_SMB_SPDS) {
			/* Read data less 16 bytes status */
			if (periph_status & IT83XX_SMB_RCS) {
				/* Disable FIFO mode to clear left count */
				IT83XX_SMB_SFFCTL &= ~IT83XX_SMB_SAFE;

				/* Peripheral A FIFO Enable */
				IT83XX_SMB_SFFCTL |= IT83XX_SMB_SAFE;
			}
			/* Controller to write data */
			else {
				for (i = 0; i < count; i++)
					/* Get data from controller to buffer */
					pbuffer[(i + w_index) &
					I2C_SIZE_MASK] = IT83XX_SMB_SLDA;
			}

			/* Reset read and write buffer index */
			buffer_index_reset();
		}
		/* Peripheral time status, timeout status occurs. */
		if (periph_status & IT83XX_SMB_STS) {
			/* Reset read and write buffer index */
			buffer_index_reset();
		}

		/* Write clear the peripheral status */
		IT83XX_SMB_SLSTA = periph_status;
	}
	/* Enhanced I2C peripheral channel D, E, F DMA mode */
	else {
		int ch, idx;

		/* Get enhanced i2c channel */
		ch = i2c_periph_ctrl[port].offset / I2C_ENHANCED_CH_INTERVAL;

		idx = i2c_periph_ctrl[port].dma_index;

		/* Interrupt pending */
		if (IT83XX_I2C_STR(ch) & IT83XX_I2C_INTPEND) {

			periph_status = IT83XX_I2C_IRQ_ST(ch);

			/* Controller to read data */
			if (periph_status & IT83XX_I2C_IDR_CLR) {
			/*
			 * TODO(b:129360157): Return buffer data by
			 * "out_data" array.
			 * Ex: Write data to buffer from 0x00 to 0xFF
			 */
				for (i = 0; i < I2C_MAX_BUFFER_SIZE; i++)
					out_data[idx][i] = i;
			}
			/* Controller to write data */
			if (periph_status & IT83XX_I2C_IDW_CLR) {
				/* Controller to write data finish flag */
				wr_done[idx] = 1;
			}
			/* Peripheral finish */
			if (periph_status & IT83XX_I2C_P_CLR) {
				if (wr_done[idx]) {
			/*
			 * TODO(b:129360157): Handle controller write
			 * data by "in_data" array.
			 */
					CPRINTS("WData: %ph",
						HEX_BUF(in_data[idx],
							I2C_MAX_BUFFER_SIZE));
					wr_done[idx] = 0;
				}
			}

			/* Write clear the peripheral status */
			IT83XX_I2C_IRQ_ST(ch) = periph_status;
		}

		/* Hardware reset */
		IT83XX_I2C_CTR(ch) |= IT83XX_I2C_HALT;
	}
}

void i2c_periph_interrupt(int port)
{
	/* Peripheral to read and write fifo data */
	i2c_peripheral_read_write_data(port);

	/* Clear the interrupt status */
	task_clear_pending_irq(i2c_periph_ctrl[port].irq);
}

void i2c_peripheral_enable(int port, uint8_t periph_addr)
{

	clock_enable_peripheral(i2c_periph_ctrl[port].clock_gate, 0, 0);

	/* I2C peripheral channel A FIFO mode */
	if (port < I2C_STANDARD_PORT_COUNT) {

		/* This field defines the SMCLK0/1/2 clock/data low timeout. */
		IT83XX_SMB_25MS = I2C_CLK_LOW_TIMEOUT;

		/* bit0 : Peripheral A FIFO Enable */
		IT83XX_SMB_SFFCTL |= IT83XX_SMB_SAFE;

		/*
		 * bit1 : Peripheral interrupt enable.
		 * bit2 : SMCLK/SMDAT will be released if timeout.
		 * bit3 : Peripheral detect STOP condition interrupt enable.
		 */
		IT83XX_SMB_SICR = 0x0E;

		/* Peripheral address 1 */
		IT83XX_SMB_RESLADR = periph_addr;

		/* Write clear all peripheral status */
		IT83XX_SMB_SLSTA = 0xE7;

		/* bit5 : Enable the SMBus peripheral device */
		IT83XX_SMB_HOCTL2(port) |= IT83XX_SMB_SLVEN;
	}
	/* Enhanced I2C peripheral channel D, E, F DMA mode */
	else {
		int ch, idx;
		uint32_t in_data_addr, out_data_addr;

		/* Get enhanced i2c channel */
		ch = i2c_periph_ctrl[port].offset / I2C_ENHANCED_CH_INTERVAL;

		idx = i2c_periph_ctrl[port].dma_index;

		switch (port) {
		case IT83XX_I2C_CH_D:
			/* Enable I2C D channel */
			IT83XX_GPIO_GRC2 |= (1 << 5);
			break;
		case IT83XX_I2C_CH_E:
			/* Enable I2C E channel */
			IT83XX_GCTRL_PMER1 |= (1 << 0);
			break;
		case IT83XX_I2C_CH_F:
			/* Enable I2C F channel */
			IT83XX_GCTRL_PMER1 |= (1 << 1);
			break;
		}

		/* Software reset */
		IT83XX_I2C_DHTR(ch) |= (1 << 7);
		IT83XX_I2C_DHTR(ch) &= ~(1 << 7);

		/* This field defines the SMCLK3/4/5 clock/data low timeout. */
		IT83XX_I2C_TOR(ch) = I2C_CLK_LOW_TIMEOUT;

		/* Bit stretching */
		IT83XX_I2C_TOS(ch) |= IT83XX_I2C_CLK_STR;

		/* Peripheral address(8-bit)*/
		IT83XX_I2C_IDR(ch) = periph_addr << 1;

		/* I2C interrupt enable and set acknowledge */
		IT83XX_I2C_CTR(ch) = IT83XX_I2C_HALT |
			IT83XX_I2C_INTEN | IT83XX_I2C_ACK;

		/*
		 * bit3 : Peripheral ID write flag
		 * bit2 : Peripheral ID read flag
		 * bit1 : Peripheral received data flag
		 * bit0 : Peripheral finish
		 */
		IT83XX_I2C_IRQ_ST(ch) = 0xFF;

		/* Clear read and write data buffer of DMA */
		memset(in_data[idx], 0, I2C_MAX_BUFFER_SIZE);
		memset(out_data[idx], 0, I2C_MAX_BUFFER_SIZE);

		if (IS_ENABLED(CHIP_ILM_DLM_ORDER)) {
			in_data_addr = (uint32_t)in_data[idx] & 0xffffff;
			out_data_addr = (uint32_t)out_data[idx] & 0xffffff;
		} else {
			in_data_addr = (uint32_t)in_data[idx] & 0xfff;
			out_data_addr = (uint32_t)out_data[idx] & 0xfff;
		}

		/* DMA write target address register */
		IT83XX_I2C_RAMHA(ch) = in_data_addr >> 8;
		IT83XX_I2C_RAMLA(ch) = in_data_addr;

		if (IS_ENABLED(CHIP_ILM_DLM_ORDER)) {
			/*
			 * DMA write target address register
			 * for high order byte
			 */
			IT83XX_I2C_RAMH2A(ch) = in_data_addr >> 16;
			/*
			 * DMA read target address register
			 * for high order byte
			 */
			IT83XX_I2C_CMD_ADDH2(ch) = out_data_addr >> 16;
			IT83XX_I2C_CMD_ADDH(ch) = out_data_addr >> 8;
			IT83XX_I2C_CMD_ADDL(ch) = out_data_addr;
		} else {
			/* DMA read target address register */
			IT83XX_I2C_RAMHA2(ch) = out_data_addr >> 8;
			IT83XX_I2C_RAMLA2(ch) = out_data_addr;
		}

		/* I2C module enable and command queue mode */
		IT83XX_I2C_CTR1(ch) = IT83XX_I2C_COMQ_EN |
			IT83XX_I2C_MDL_EN;
	}
}

static void i2c_peripheral_init(void)
{
	int  i, p;

	/* DLM 52k~56k size select enable */
	IT83XX_GCTRL_MCCR2 |= (1 << 4);

	/* Enable I2C Peripheral function */
	for (i = 0; i < i2c_periphs_used; i++) {

		/* I2c peripheral port mapping. */
		p = i2c_periph_ports[i].port;

		/* To enable peripheral ch[x] */
		i2c_peripheral_enable(p, i2c_periph_ports[i].addr);

		/* Clear the interrupt status */
		task_clear_pending_irq(i2c_periph_ctrl[p].irq);

		/* enable i2c interrupt */
		task_enable_irq(i2c_periph_ctrl[p].irq);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_peripheral_init, HOOK_PRIO_POST_I2C);
