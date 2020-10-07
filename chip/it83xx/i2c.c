/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* Default maximum time we allow for an I2C transfer */
#define I2C_TIMEOUT_DEFAULT_US (100 * MSEC)

enum enhanced_i2c_transfer_direct {
	TX_DIRECT,
	RX_DIRECT,
};

enum i2c_host_status {
	/* Host busy */
	HOSTA_HOBY = 0x01,
	/* Finish Interrupt */
	HOSTA_FINTR = 0x02,
	/* Device error */
	HOSTA_DVER = 0x04,
	/* Bus error */
	HOSTA_BSER = 0x08,
	/* Fail */
	HOSTA_FAIL = 0x10,
	/* Not response ACK */
	HOSTA_NACK = 0x20,
	/* Time-out error */
	HOSTA_TMOE = 0x40,
	/* Byte done status */
	HOSTA_BDS = 0x80,
	/* Error bit is set */
	HOSTA_ANY_ERROR = (HOSTA_DVER | HOSTA_BSER |
				HOSTA_FAIL | HOSTA_NACK | HOSTA_TMOE),
	/* W/C for next byte */
	HOSTA_NEXT_BYTE = HOSTA_BDS,
	/* W/C host status register */
	HOSTA_ALL_WC_BIT = (HOSTA_FINTR | HOSTA_ANY_ERROR | HOSTA_BDS),
};

enum enhanced_i2c_host_status {
	/* ACK receive */
	E_HOSTA_ACK = 0x01,
	/* Interrupt pending */
	E_HOSTA_INTP = 0x02,
	/* Read/Write */
	E_HOSTA_RW = 0x04,
	/* Time out error */
	E_HOSTA_TMOE = 0x08,
	/* Arbitration lost */
	E_HOSTA_ARB = 0x10,
	/* Bus busy */
	E_HOSTA_BB = 0x20,
	/* Address match */
	E_HOSTA_AM = 0x40,
	/* Byte done status */
	E_HOSTA_BDS = 0x80,
	/* time out or lost arbitration */
	E_HOSTA_ANY_ERROR = (E_HOSTA_TMOE | E_HOSTA_ARB),
	/* Byte transfer done and ACK receive */
	E_HOSTA_BDS_AND_ACK = (E_HOSTA_BDS | E_HOSTA_ACK),
};

enum enhanced_i2c_ctl {
	/* Hardware reset */
	E_HW_RST = 0x01,
	/* Stop */
	E_STOP = 0x02,
	/* Start & Repeat start */
	E_START = 0x04,
	/* Acknowledge */
	E_ACK = 0x08,
	/* State reset */
	E_STS_RST = 0x10,
	/* Mode select */
	E_MODE_SEL = 0x20,
	/* I2C interrupt enable */
	E_INT_EN = 0x40,
	/* 0 : Standard mode , 1 : Receive mode */
	E_RX_MODE = 0x80,
	/* State reset and hardware reset */
	E_STS_AND_HW_RST = (E_STS_RST | E_HW_RST),
	/* Generate start condition and transmit slave address */
	E_START_ID = (E_INT_EN | E_MODE_SEL | E_ACK | E_START | E_HW_RST),
	/* Generate stop condition */
	E_FINISH = (E_INT_EN | E_MODE_SEL | E_ACK | E_STOP | E_HW_RST),
};

enum i2c_reset_cause {
	I2C_RC_NO_IDLE_FOR_START = 1,
	I2C_RC_TIMEOUT,
};

struct i2c_ch_freq {
	int kbps;
	uint8_t freq_set;
};

static const struct i2c_ch_freq i2c_freq_select[] = {
	{ 50,   1},
	{ 100,  2},
	{ 400,  3},
	{ 1000, 4},
};

struct i2c_pin {
	volatile uint8_t *pin_clk;
	volatile uint8_t *pin_data;
	volatile uint8_t *pin_clk_ctrl;
	volatile uint8_t *pin_data_ctrl;
	volatile uint8_t *mirror_clk;
	volatile uint8_t *mirror_data;
	uint8_t clk_mask;
	uint8_t data_mask;
};

static const struct i2c_pin i2c_pin_regs[] = {
	{ &IT83XX_GPIO_GPCRB3, &IT83XX_GPIO_GPCRB4,
		&IT83XX_GPIO_GPDRB, &IT83XX_GPIO_GPDRB,
		&IT83XX_GPIO_GPDMRB, &IT83XX_GPIO_GPDMRB,
		0x08, 0x10},
	{ &IT83XX_GPIO_GPCRC1, &IT83XX_GPIO_GPCRC2,
		&IT83XX_GPIO_GPDRC, &IT83XX_GPIO_GPDRC,
		&IT83XX_GPIO_GPDMRC, &IT83XX_GPIO_GPDMRC,
		0x02, 0x04},
#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
	{ &IT83XX_GPIO_GPCRC7, &IT83XX_GPIO_GPCRF7,
		&IT83XX_GPIO_GPDRC, &IT83XX_GPIO_GPDRF,
		&IT83XX_GPIO_GPDMRC, &IT83XX_GPIO_GPDMRF,
		0x80, 0x80},
#else
	{ &IT83XX_GPIO_GPCRF6, &IT83XX_GPIO_GPCRF7,
		&IT83XX_GPIO_GPDRF, &IT83XX_GPIO_GPDRF,
		&IT83XX_GPIO_GPDMRF, &IT83XX_GPIO_GPDMRF,
		0x40, 0x80},
#endif
	{ &IT83XX_GPIO_GPCRH1, &IT83XX_GPIO_GPCRH2,
		&IT83XX_GPIO_GPDRH, &IT83XX_GPIO_GPDRH,
		&IT83XX_GPIO_GPDMRH, &IT83XX_GPIO_GPDMRH,
		0x02, 0x04},
	{ &IT83XX_GPIO_GPCRE0, &IT83XX_GPIO_GPCRE7,
		&IT83XX_GPIO_GPDRE, &IT83XX_GPIO_GPDRE,
		&IT83XX_GPIO_GPDMRE, &IT83XX_GPIO_GPDMRE,
		0x01, 0x80},
	{ &IT83XX_GPIO_GPCRA4, &IT83XX_GPIO_GPCRA5,
		&IT83XX_GPIO_GPDRA, &IT83XX_GPIO_GPDRA,
		&IT83XX_GPIO_GPDMRA, &IT83XX_GPIO_GPDMRA,
		0x10, 0x20},
};

struct i2c_ctrl_t {
	uint8_t irq;
	enum clock_gate_offsets clock_gate;
	int reg_shift;
};

const struct i2c_ctrl_t i2c_ctrl_regs[] = {
	{IT83XX_IRQ_SMB_A, CGC_OFFSET_SMBA, -1},
	{IT83XX_IRQ_SMB_B, CGC_OFFSET_SMBB, -1},
	{IT83XX_IRQ_SMB_C, CGC_OFFSET_SMBC, -1},
	{IT83XX_IRQ_SMB_D, CGC_OFFSET_SMBD, 3},
	{IT83XX_IRQ_SMB_E, CGC_OFFSET_SMBE, 0},
	{IT83XX_IRQ_SMB_F, CGC_OFFSET_SMBF, 1},
};

enum i2c_ch_status {
	I2C_CH_NORMAL = 0,
	I2C_CH_REPEAT_START,
	I2C_CH_WAIT_READ,
	I2C_CH_WAIT_NEXT_XFER,
};

/* I2C port state data */
struct i2c_port_data {
	const uint8_t *out;  /* Output data pointer */
	int out_size;        /* Output data to transfer, in bytes */
	uint8_t *in;         /* Input data pointer */
	int in_size;         /* Input data to transfer, in bytes */
	int flags;           /* Flags (I2C_XFER_*) */
	int widx;            /* Index into output data */
	int ridx;            /* Index into input data */
	int err;             /* Error code, if any */
	uint8_t addr_8bit;   /* address of device */
	uint32_t timeout_us; /* Transaction timeout, or 0 to use default */
	uint8_t freq;        /* Frequency setting */

	enum i2c_ch_status i2ccs;
	/* Task waiting on port, or TASK_ID_INVALID if none. */
	volatile int task_waiting;
};
static struct i2c_port_data pdata[I2C_PORT_COUNT];

static int i2c_ch_reg_shift(int p)
{
	/*
	 * only enhanced port needs to be changed the parameter of registers
	 */
	ASSERT(p >= I2C_STANDARD_PORT_COUNT && p < I2C_PORT_COUNT);

	/*
	 * The registers of i2c enhanced ports are not sequential.
	 * This routine transfers the i2c port number to related
	 * parameter of registers.
	 *
	 * IT83xx chip : i2c enhanced ports - channel D,E,F
	 * channel D registers : 0x3680 ~ 0x36FF
	 * channel E registers : 0x3500 ~ 0x357F
	 * channel F registers : 0x3580 ~ 0x35FF
	 */
	return i2c_ctrl_regs[p].reg_shift;
}

static void i2c_reset(int p, int cause)
{
	int p_ch;

	if (p < I2C_STANDARD_PORT_COUNT) {
		/* bit1, kill current transaction. */
		IT83XX_SMB_HOCTL(p) = 0x2;
		IT83XX_SMB_HOCTL(p) = 0;
		/* W/C host status register */
		IT83XX_SMB_HOSTA(p) = HOSTA_ALL_WC_BIT;
	} else {
		/* Shift register */
		p_ch = i2c_ch_reg_shift(p);
		/* State reset and hardware reset */
		IT83XX_I2C_CTR(p_ch) = E_STS_AND_HW_RST;
	}
	CPRINTS("I2C ch%d reset cause %d", p, cause);
}

static void i2c_r_last_byte(int p)
{
	struct i2c_port_data *pd = pdata + p;

	/*
	 * bit5, The firmware shall write 1 to this bit
	 * when the next byte will be the last byte for i2c read.
	 */
	if ((pd->flags & I2C_XFER_STOP) && (pd->ridx == pd->in_size - 1))
		IT83XX_SMB_HOCTL(p) |= 0x20;
}

static void i2c_w2r_change_direction(int p)
{
	/* I2C switch direction */
	if (IT83XX_SMB_HOCTL2(p) & 0x08) {
		i2c_r_last_byte(p);
		IT83XX_SMB_HOSTA(p) = HOSTA_NEXT_BYTE;
	} else {
		/*
		 * bit2, I2C switch direction wait.
		 * bit3, I2C switch direction enable.
		 */
		IT83XX_SMB_HOCTL2(p) |= 0x0C;
		IT83XX_SMB_HOSTA(p) = HOSTA_NEXT_BYTE;
		i2c_r_last_byte(p);
		IT83XX_SMB_HOCTL2(p) &= ~0x04;
	}
}

static void i2c_pio_trans_data(int p, enum enhanced_i2c_transfer_direct direct,
			       uint8_t data, int first_byte)
{
	struct i2c_port_data *pd = pdata + p;
	int p_ch;
	int nack = 0;

	/* Shift register */
	p_ch = i2c_ch_reg_shift(p);

	if (first_byte) {
		/* First byte must be slave address. */
		IT83XX_I2C_DTR(p_ch) =
			data | (direct == RX_DIRECT ? BIT(0) : 0);
		/* start or repeat start signal. */
		IT83XX_I2C_CTR(p_ch) = E_START_ID;
	} else {
		if (direct == TX_DIRECT)
			/* Transmit data */
			IT83XX_I2C_DTR(p_ch) = data;
		else {
			/*
			 * Receive data.
			 * Last byte should be NACK in the end of read cycle
			 */
			if (((pd->ridx + 1) == pd->in_size) &&
				(pd->flags & I2C_XFER_STOP))
				nack = 1;
		}
		/* Set hardware reset to start next transmission */
		IT83XX_I2C_CTR(p_ch) =
			E_INT_EN | E_MODE_SEL | E_HW_RST | (nack ? 0 : E_ACK);
	}
}

static int i2c_tran_write(int p)
{
	struct i2c_port_data *pd = pdata + p;

	if (pd->flags & I2C_XFER_START) {
		/* i2c enable */
		IT83XX_SMB_HOCTL2(p) = 0x13;
		/*
		 * bit0, Direction of the host transfer.
		 * bit[1:7}, Address of the targeted slave.
		 */
		IT83XX_SMB_TRASLA(p) = pd->addr_8bit;
		/* Send first byte */
		IT83XX_SMB_HOBDB(p) = *(pd->out++);
		pd->widx++;
		/* clear start flag */
		pd->flags &= ~I2C_XFER_START;
		/*
		 * bit0, Host interrupt enable.
		 * bit[2:4}, Extend command.
		 * bit6, start.
		 */
		IT83XX_SMB_HOCTL(p) = 0x5D;
	} else {
		/* Host has completed the transmission of a byte */
		if (IT83XX_SMB_HOSTA(p) & HOSTA_BDS) {
			if (pd->widx < pd->out_size) {
				/* Send next byte */
				IT83XX_SMB_HOBDB(p) = *(pd->out++);
				pd->widx++;
				/* W/C byte done for next byte */
				IT83XX_SMB_HOSTA(p) = HOSTA_NEXT_BYTE;
				if (pd->i2ccs == I2C_CH_REPEAT_START) {
					pd->i2ccs = I2C_CH_NORMAL;
					task_enable_irq(i2c_ctrl_regs[p].irq);
				}
			} else {
				/* done */
				pd->out_size = 0;
				if (pd->in_size > 0) {
					/* write to read */
					i2c_w2r_change_direction(p);
				} else {
					if (pd->flags & I2C_XFER_STOP) {
						/* set I2C_EN = 0 */
						IT83XX_SMB_HOCTL2(p) = 0x11;
						/* W/C byte done for finish */
						IT83XX_SMB_HOSTA(p) =
							HOSTA_NEXT_BYTE;
					} else {
						pd->i2ccs = I2C_CH_REPEAT_START;
						return 0;
					}
				}
			}
		}
	}
	return 1;
}

static int i2c_tran_read(int p)
{
	struct i2c_port_data *pd = pdata + p;

	if (pd->flags & I2C_XFER_START) {
		/* i2c enable */
		IT83XX_SMB_HOCTL2(p) = 0x13;
		/*
		 * bit0, Direction of the host transfer.
		 * bit[1:7}, Address of the targeted slave.
		 */
		IT83XX_SMB_TRASLA(p) = pd->addr_8bit | 0x01;
		/* clear start flag */
		pd->flags &= ~I2C_XFER_START;
		/*
		 * bit0, Host interrupt enable.
		 * bit[2:4}, Extend command.
		 * bit5, The firmware shall write 1 to this bit
		 *       when the next byte will be the last byte.
		 * bit6, start.
		 */
		if ((1 == pd->in_size) && (pd->flags & I2C_XFER_STOP))
			IT83XX_SMB_HOCTL(p) = 0x7D;
		else
			IT83XX_SMB_HOCTL(p) = 0x5D;
	} else {
		if ((pd->i2ccs == I2C_CH_REPEAT_START) ||
			(pd->i2ccs == I2C_CH_WAIT_READ)) {
			if (pd->i2ccs == I2C_CH_REPEAT_START) {
				/* write to read */
				i2c_w2r_change_direction(p);
			} else {
				/* For last byte */
				i2c_r_last_byte(p);
				/* W/C for next byte */
				IT83XX_SMB_HOSTA(p) = HOSTA_NEXT_BYTE;
			}
			pd->i2ccs = I2C_CH_NORMAL;
			task_enable_irq(i2c_ctrl_regs[p].irq);
		} else if (IT83XX_SMB_HOSTA(p) & HOSTA_BDS) {
			if (pd->ridx < pd->in_size) {
				/* To get received data. */
				*(pd->in++) = IT83XX_SMB_HOBDB(p);
				pd->ridx++;
				/* For last byte */
				i2c_r_last_byte(p);
				/* done */
				if (pd->ridx == pd->in_size) {
					pd->in_size = 0;
					if (pd->flags & I2C_XFER_STOP) {
						/* W/C for finish */
						IT83XX_SMB_HOSTA(p) =
							HOSTA_NEXT_BYTE;
					} else {
						pd->i2ccs = I2C_CH_WAIT_READ;
						return 0;
					}
				} else {
					/* W/C for next byte */
					IT83XX_SMB_HOSTA(p) = HOSTA_NEXT_BYTE;
				}
			}
		}
	}
	return 1;
}

static void enhanced_i2c_start(int p)
{
	/* Shift register */
	int p_ch = i2c_ch_reg_shift(p);

	/* State reset and hardware reset */
	IT83XX_I2C_CTR(p_ch) = E_STS_AND_HW_RST;
	/* Set i2c frequency */
	IT83XX_I2C_PSR(p_ch) = pdata[p].freq;
	IT83XX_I2C_HSPR(p_ch) = pdata[p].freq;
	/*
	 * Set time out register.
	 * I2C D/E/F clock/data low timeout.
	 */
	IT83XX_I2C_TOR(p_ch) = I2C_CLK_LOW_TIMEOUT;
	/* bit1: Enable enhanced i2c module */
	IT83XX_I2C_CTR1(p_ch) = BIT(1);
}

static int enhanced_i2c_tran_write(int p)
{
	struct i2c_port_data *pd = pdata + p;
	uint8_t out_data;
	int p_ch;

	/* Shift register */
	p_ch = i2c_ch_reg_shift(p);

	if (pd->flags & I2C_XFER_START) {
		/* Clear start bit */
		pd->flags &= ~I2C_XFER_START;
		enhanced_i2c_start(p);
		/* Send ID */
		i2c_pio_trans_data(p, TX_DIRECT, pd->addr_8bit, 1);
	} else {
		/* Host has completed the transmission of a byte */
		if (pd->widx < pd->out_size) {
			out_data = *(pd->out++);
			pd->widx++;

			/* Send Byte */
			i2c_pio_trans_data(p, TX_DIRECT, out_data, 0);
			if (pd->i2ccs == I2C_CH_WAIT_NEXT_XFER) {
				pd->i2ccs = I2C_CH_NORMAL;
				task_enable_irq(i2c_ctrl_regs[p].irq);
			}
		} else {
			/* done */
			pd->out_size = 0;
			if (pd->in_size > 0) {
				/* Write to read protocol */
				pd->i2ccs = I2C_CH_REPEAT_START;
				/* Repeat Start */
				i2c_pio_trans_data(p, RX_DIRECT,
						   pd->addr_8bit, 1);
			} else {
				if (pd->flags & I2C_XFER_STOP) {
					IT83XX_I2C_CTR(p_ch) = E_FINISH;
					/* wait for stop bit interrupt*/
					return 1;
				}
				/* Direct write with direct read */
				pd->i2ccs = I2C_CH_WAIT_NEXT_XFER;
				return 0;
			}
		}
	}
	return 1;
}

static int enhanced_i2c_tran_read(int p)
{
	struct i2c_port_data *pd = pdata + p;
	uint8_t in_data = 0;
	int p_ch;

	/* Shift register */
	p_ch = i2c_ch_reg_shift(p);

	if (pd->flags & I2C_XFER_START) {
		/* clear start flag */
		pd->flags &= ~I2C_XFER_START;
		enhanced_i2c_start(p);
		/* Direct read  */
		pd->i2ccs = I2C_CH_WAIT_READ;
		/* Send ID */
		i2c_pio_trans_data(p, RX_DIRECT, pd->addr_8bit, 1);
	} else {
		if (pd->i2ccs) {
			if (pd->i2ccs == I2C_CH_REPEAT_START) {
				pd->i2ccs = I2C_CH_NORMAL;
				/* Receive data */
				i2c_pio_trans_data(p, RX_DIRECT, in_data, 0);
			} else if (pd->i2ccs == I2C_CH_WAIT_READ) {
				pd->i2ccs = I2C_CH_NORMAL;
				/* Receive data */
				i2c_pio_trans_data(p, RX_DIRECT, in_data, 0);
				/* Turn on irq before next direct read */
				task_enable_irq(i2c_ctrl_regs[p].irq);
			} else {
				/* Write to read */
				pd->i2ccs = I2C_CH_WAIT_READ;
				/* Send ID */
				i2c_pio_trans_data(p, RX_DIRECT,
						   pd->addr_8bit, 1);
				task_enable_irq(i2c_ctrl_regs[p].irq);
			}
		} else {
			if (pd->ridx < pd->in_size) {
				/* read data */
				*(pd->in++) = IT83XX_I2C_DRR(p_ch);
				pd->ridx++;

				/* done */
				if (pd->ridx == pd->in_size) {
					pd->in_size = 0;
					if (pd->flags & I2C_XFER_STOP) {
						pd->i2ccs = I2C_CH_NORMAL;
						IT83XX_I2C_CTR(p_ch) = E_FINISH;
						/* wait for stop bit interrupt*/
						return 1;
					}
					/* End the transaction */
					pd->i2ccs = I2C_CH_WAIT_READ;
					return 0;
				}
				/* read next byte */
				i2c_pio_trans_data(p, RX_DIRECT, in_data, 0);
			}
		}
	}
	return 1;
}

static int enhanced_i2c_error(int p)
{
	struct i2c_port_data *pd = pdata + p;
	/* Shift register */
	int p_ch = i2c_ch_reg_shift(p);
	int i2c_str = IT83XX_I2C_STR(p_ch);

	if (i2c_str & E_HOSTA_ANY_ERROR) {
		pd->err = i2c_str & E_HOSTA_ANY_ERROR;
	/* device does not respond ACK */
	} else if ((i2c_str & E_HOSTA_BDS_AND_ACK) == E_HOSTA_BDS) {
		if (IT83XX_I2C_CTR(p_ch) & E_ACK)
			pd->err = E_HOSTA_ACK;
	}

	return pd->err;
}

static int i2c_transaction(int p)
{
	struct i2c_port_data *pd = pdata + p;
	int p_ch;

	if (p < I2C_STANDARD_PORT_COUNT) {
		/* any error */
		if (IT83XX_SMB_HOSTA(p) & HOSTA_ANY_ERROR) {
			pd->err = (IT83XX_SMB_HOSTA(p) & HOSTA_ANY_ERROR);
		} else {
			/* i2c write */
			if (pd->out_size)
				return i2c_tran_write(p);
			/* i2c read */
			else if (pd->in_size)
				return i2c_tran_read(p);
			/* wait finish */
			if (!(IT83XX_SMB_HOSTA(p) & HOSTA_FINTR))
				return 1;
		}
		/* W/C */
		IT83XX_SMB_HOSTA(p) = HOSTA_ALL_WC_BIT;
		/* disable the SMBus host interface */
		IT83XX_SMB_HOCTL2(p) = 0x00;
	} else {
		/* no error */
		if (!(enhanced_i2c_error(p))) {
			/* i2c write */
			if (pd->out_size)
				return enhanced_i2c_tran_write(p);
			/* i2c read */
			else if (pd->in_size)
				return enhanced_i2c_tran_read(p);
		}
		p_ch = i2c_ch_reg_shift(p);
		IT83XX_I2C_CTR(p_ch) = E_STS_AND_HW_RST;
		IT83XX_I2C_CTR1(p_ch) = 0;
	}
	/* done doing work */
	return 0;
}

int i2c_is_busy(int port)
{
	int p_ch;

	if (port < I2C_STANDARD_PORT_COUNT)
		return (IT83XX_SMB_HOSTA(port) &
			(HOSTA_HOBY | HOSTA_ALL_WC_BIT));

	p_ch = i2c_ch_reg_shift(port);
	return (IT83XX_I2C_STR(p_ch) & E_HOSTA_BB);
}

int chip_i2c_xfer(int port, uint16_t slave_addr_flags,
		  const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	struct i2c_port_data *pd = pdata + port;
	uint32_t events = 0;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	if (pd->i2ccs) {
		if ((flags & I2C_XFER_SINGLE) == I2C_XFER_SINGLE)
			flags &= ~I2C_XFER_START;
	}

	/* Copy data to port struct */
	pd->out = out;
	pd->out_size = out_size;
	pd->in = in;
	pd->in_size = in_size;
	pd->flags = flags;
	pd->widx = 0;
	pd->ridx = 0;
	pd->err = 0;
	pd->addr_8bit = I2C_GET_ADDR(slave_addr_flags) << 1;

	/* Make sure we're in a good state to start */
	if ((flags & I2C_XFER_START) && (i2c_is_busy(port)
		|| (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {

		/* Attempt to unwedge the port. */
		pd->err = i2c_unwedge(port);

		/* reset i2c port */
		i2c_reset(port, I2C_RC_NO_IDLE_FOR_START);

		/* Return if port is still wedged */
		if (pd->err)
			return pd->err;
	}

	pd->task_waiting = task_get_current();
	if (pd->flags & I2C_XFER_START) {
		pd->i2ccs = I2C_CH_NORMAL;
		/* enable i2c interrupt */
		task_clear_pending_irq(i2c_ctrl_regs[port].irq);
		task_enable_irq(i2c_ctrl_regs[port].irq);
	}
	/* Start transaction */
	i2c_transaction(port);
	/* Wait for transfer complete or timeout */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE, pd->timeout_us);
	/* disable i2c interrupt */
	task_disable_irq(i2c_ctrl_regs[port].irq);
	pd->task_waiting = TASK_ID_INVALID;

	/* Handle timeout */
	if (!(events & TASK_EVENT_I2C_IDLE)) {
		pd->err = EC_ERROR_TIMEOUT;
		/* reset i2c port */
		i2c_reset(port, I2C_RC_TIMEOUT);
	}

	/* reset i2c channel status */
	if (pd->err)
		pd->i2ccs = I2C_CH_NORMAL;

	return pd->err;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
		return !!(*i2c_pin_regs[port].mirror_clk &
			i2c_pin_regs[port].clk_mask);

	/* If no SCL pin defined for this port, then return 1 to appear idle */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
		return !!(*i2c_pin_regs[port].mirror_data &
			i2c_pin_regs[port].data_mask);

	/* If no SDA pin defined for this port, then return 1 to appear idle */
	return 1;
}

int i2c_get_line_levels(int port)
{
	int pin_sts = 0;

	if (port < I2C_STANDARD_PORT_COUNT)
		return IT83XX_SMB_SMBPCTL(port) & 0x03;

	if (*i2c_pin_regs[port].mirror_clk & i2c_pin_regs[port].clk_mask)
		pin_sts |= I2C_LINE_SCL_HIGH;
	if (*i2c_pin_regs[port].mirror_data & i2c_pin_regs[port].data_mask)
		pin_sts |= I2C_LINE_SDA_HIGH;

	return pin_sts;
}

void i2c_set_timeout(int port, uint32_t timeout)
{
	pdata[port].timeout_us = timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

void i2c_interrupt(int port)
{
	int id = pdata[port].task_waiting;

	/* Clear the interrupt status */
	task_clear_pending_irq(i2c_ctrl_regs[port].irq);

	/* If no task is waiting, just return */
	if (id == TASK_ID_INVALID)
		return;

	/* If done doing work, wake up the task waiting for the transfer */
	if (!i2c_transaction(port)) {
		task_disable_irq(i2c_ctrl_regs[port].irq);
		task_set_event(id, TASK_EVENT_I2C_IDLE, 0);
	}
}

/*
 * Set i2c standard port (A, B, or C) runs at 400kHz by using timing registers
 * (offset 0h ~ 7h).
 */
static void i2c_standard_port_timing_regs_400khz(int port)
{
	/* Port clock frequency depends on setting of timing registers. */
	IT83XX_SMB_SCLKTS(port) = 0;
	/* Suggested setting of timing registers of 400kHz. */
	IT83XX_SMB_4P7USL = 0x5;
	IT83XX_SMB_4P0USL = 0x1;
	IT83XX_SMB_300NS = 0x1;
	IT83XX_SMB_250NS = 0x2;
	IT83XX_SMB_45P3USL = 0x6a;
	IT83XX_SMB_45P3USH = 0x1;
	IT83XX_SMB_4P7A4P0H = 0;
}

/* Set clock frequency for i2c port A, B , or C */
static void i2c_standard_port_set_frequency(int port, int freq_khz)
{
	/*
	 * If port's clock frequency is 400kHz, we use timing registers
	 * for setting. So we can adjust tlow to meet timing.
	 * The others use basic 50/100/1000 KHz setting.
	 */
	if (freq_khz == 400) {
		i2c_standard_port_timing_regs_400khz(port);
	} else {
		for (int f = ARRAY_SIZE(i2c_freq_select) - 1; f >= 0; f--) {
			if (freq_khz >= i2c_freq_select[f].kbps) {
				IT83XX_SMB_SCLKTS(port) =
						i2c_freq_select[f].freq_set;
				break;
			}
		}
	}

	/* This field defines the SMCLK0/1/2 clock/data low timeout. */
	IT83XX_SMB_25MS = I2C_CLK_LOW_TIMEOUT;
}

/* Set clock frequency for i2c port D, E , or F */
static void i2c_enhanced_port_set_frequency(int port, int freq_khz)
{
	int port_reg_shift, clk_div, psr;

	/* Get base address of i2c enhanced port's registers. */
	port_reg_shift = i2c_ch_reg_shift(port);
	/*
	 * Let psr(Prescale) = IT83XX_I2C_PSR(port_reg_shift)
	 * Then, 1 SCL cycle = 2 x (psr + 2) x SMBus clock cycle
	 * SMBus clock = PLL_CLOCK / clk_div
	 * SMBus clock cycle = 1 / SMBus clock
	 * 1 SCL cycle = 1 / (1000 x freq)
	 * 1 / (1000 x freq) = 2 x (psr + 2) x (1 / (PLL_CLOCK / clk_div))
	 * psr = ((PLL_CLOCK / clk_div) x (1 / (1000 x freq)) x (1 / 2)) - 2
	 */
	if (freq_khz) {
		/* Get SMBus clock divide value */
		clk_div = (IT83XX_ECPM_SCDCR2 & 0x0F) + 1;
		/* Calculate PSR value */
		psr = (PLL_CLOCK / (clk_div * (2 * 1000 * freq_khz))) - 2;
		/* Set psr value under 0xFD */
		if (psr > 0xFD)
			psr = 0xFD;

		/* Set I2C Speed */
		IT83XX_I2C_PSR(port_reg_shift) = (psr & 0xFF);
		IT83XX_I2C_HSPR(port_reg_shift) = (psr & 0xFF);
		/* Backup */
		pdata[port].freq = (psr & 0xFF);
	}
}

static void i2c_freq_changed(void)
{
	int i, freq, port;

	/* Set clock frequency for I2C ports */
	for (i = 0; i < i2c_ports_used; i++) {
		freq = i2c_ports[i].kbps;
		port = i2c_ports[i].port;
		if (port < I2C_STANDARD_PORT_COUNT)
			i2c_standard_port_set_frequency(port, freq);
		else
			i2c_enhanced_port_set_frequency(port, freq);
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

void i2c_init(void)
{
	int i, p, p_ch;

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

#ifdef CONFIG_IT83XX_SMCLK2_ON_GPC7
	/* bit7, 0: SMCLK2 is located on GPF6, 1: SMCLK2 is located on GPC7 */
	IT83XX_GPIO_GRC7 |= 0x80;
#endif

	/* Enable I2C function. */
	for (i = 0; i < i2c_ports_used; i++) {
		/* I2c port mapping. */
		p = i2c_ports[i].port;

		clock_enable_peripheral(i2c_ctrl_regs[p].clock_gate, 0, 0);

		if (p < I2C_STANDARD_PORT_COUNT) {
			/*
			 * bit0, The SMBus host interface is enabled.
			 * bit1, Enable to communicate with I2C device
			 *        and support I2C-compatible cycles.
			 * bit4, This bit controls the reset mechanism
			 *        of SMBus master to handle the SMDAT
			 *        line low if 25ms reg timeout.
			 */
			IT83XX_SMB_HOCTL2(p) = 0x11;
			/*
			 * bit1, Kill SMBus host transaction.
			 * bit0, Enable the interrupt for the master interface.
			 */
			IT83XX_SMB_HOCTL(p) = 0x03;
			IT83XX_SMB_HOCTL(p) = 0x01;
			/* W/C host status register */
			IT83XX_SMB_HOSTA(p) = HOSTA_ALL_WC_BIT;
			IT83XX_SMB_HOCTL2(p) = 0x00;
		} else {
			/* Shift register */
			p_ch = i2c_ch_reg_shift(p);
			switch (p) {
			case IT83XX_I2C_CH_D:
				#ifndef CONFIG_UART_HOST
				/* Enable SMBus D channel */
				IT83XX_GPIO_GRC2 |= 0x20;
				#endif
				break;
			case IT83XX_I2C_CH_E:
				/* Enable SMBus E channel */
				IT83XX_GCTRL_PMER1 |= 0x01;
				break;
			case IT83XX_I2C_CH_F:
				/* Enable SMBus F channel */
				IT83XX_GCTRL_PMER1 |= 0x02;
				break;
			}
			/* Software reset */
			IT83XX_I2C_DHTR(p_ch) |= 0x80;
			IT83XX_I2C_DHTR(p_ch) &= 0x7F;
			/* State reset and hardware reset */
			IT83XX_I2C_CTR(p_ch) = E_STS_AND_HW_RST;
			/* bit1, Module enable */
			IT83XX_I2C_CTR1(p_ch) = 0;
		}
		pdata[i].task_waiting = TASK_ID_INVALID;
	}

	i2c_freq_changed();

	for (i = 0; i < I2C_PORT_COUNT; i++) {
		/* Use default timeout */
		i2c_set_timeout(i, 0);
	}
}
