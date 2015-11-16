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

/*
 * The count number of the counter for 25 ms register.
 * The 25 ms register is calculated by (count number *1.024 kHz).
 */
#define I2C_CLK_LOW_TIMEOUT  25 /* ~= 25ms */

/* Default maximum time we allow for an I2C transfer */
#define I2C_TIMEOUT_DEFAULT_US (100 * MSEC)

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

	HOSTA_NO_FINISH = 0xFF,
};

enum i2c_host_status_mask {
	HOSTA_ANY_ERROR = (HOSTA_DVER | HOSTA_BSER |
				HOSTA_FAIL | HOSTA_NACK | HOSTA_TMOE),
	HOSTA_NEXT_BYTE = HOSTA_BDS,
	HOSTA_ALL_WC_BIT = (HOSTA_FINTR | HOSTA_ANY_ERROR | HOSTA_BDS),
};

enum i2c_reset_cause {
	I2C_RC_NO_IDLE_FOR_START = 1,
	I2C_RC_TIMEOUT,
};

struct i2c_ch_freq {
	int kpbs;
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
};

struct i2c_ctrl_t {
	uint8_t irq;
	enum clock_gate_offsets clock_gate;
};

const struct i2c_ctrl_t i2c_ctrl_regs[] = {
	{IT83XX_IRQ_SMB_A, CGC_OFFSET_SMBA},
	{IT83XX_IRQ_SMB_B, CGC_OFFSET_SMBB},
	{IT83XX_IRQ_SMB_C, CGC_OFFSET_SMBC},
};

enum i2c_ch_status {
	I2C_CH_NORMAL = 0,
	I2C_CH_W2R,
	I2C_CH_WAIT_READ,
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
	uint8_t addr;        /* address of device */
	uint32_t timeout_us; /* Transaction timeout, or 0 to use default */

	enum i2c_ch_status i2ccs;
	/* Task waiting on port, or TASK_ID_INVALID if none. */
	int task_waiting;
};
static struct i2c_port_data pdata[I2C_PORT_COUNT];

static void i2c_reset(int p, int cause)
{
	/* bit1, kill current transaction. */
	IT83XX_SMB_HOCTL(p) |= 0x02;
	IT83XX_SMB_HOCTL(p) &= ~0x02;

	/* Disable the SMBus host interface */
	IT83XX_SMB_HOCTL2(p) = 0x00;

	/* clk pin output high */
	*i2c_pin_regs[p].pin_clk = 0x40;
	*i2c_pin_regs[p].pin_clk_ctrl |= i2c_pin_regs[p].clk_mask;

	udelay(16);

	/* data pin output high */
	*i2c_pin_regs[p].pin_data = 0x40;
	*i2c_pin_regs[p].pin_data_ctrl |= i2c_pin_regs[p].data_mask;

	udelay(500);
	/* start condition */
	*i2c_pin_regs[p].pin_data_ctrl &= ~i2c_pin_regs[p].data_mask;
	udelay(1000);
	/* stop condition */
	*i2c_pin_regs[p].pin_data_ctrl |= i2c_pin_regs[p].data_mask;
	udelay(500);

	/* I2C function */
	*i2c_pin_regs[p].pin_clk = 0x00;
	*i2c_pin_regs[p].pin_data = 0x00;

	/* Enable the SMBus host interface */
	IT83XX_SMB_HOCTL2(p) = 0x11;

	/* W/C host status register */
	IT83XX_SMB_HOSTA(p) = HOSTA_ALL_WC_BIT;

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
		IT83XX_SMB_TRASLA(p) = pd->addr;
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
						pd->i2ccs = I2C_CH_W2R;
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
		IT83XX_SMB_TRASLA(p) = pd->addr | 0x01;
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
		if ((pd->i2ccs == I2C_CH_W2R) ||
			(pd->i2ccs == I2C_CH_WAIT_READ)) {
			if (pd->i2ccs == I2C_CH_W2R) {
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

static int i2c_transaction(int p)
{
	struct i2c_port_data *pd = pdata + p;

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
	/* done doing work */
	return 0;
}

int i2c_is_busy(int port)
{
	return IT83XX_SMB_HOSTA(port) & HOSTA_HOBY;
}

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags)
{
	struct i2c_port_data *pd = pdata + port;
	uint32_t events = 0;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	if ((pd->i2ccs == I2C_CH_W2R) || (pd->i2ccs == I2C_CH_WAIT_READ)) {
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
	pd->addr = slave_addr;

	/* Make sure we're in a good state to start */
	if ((flags & I2C_XFER_START) && (i2c_is_busy(port)
			|| (IT83XX_SMB_HOSTA(port) & HOSTA_ALL_WC_BIT)
			|| (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);
		/* reset i2c port */
		i2c_reset(port, I2C_RC_NO_IDLE_FOR_START);
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
	events = task_wait_event(pd->timeout_us);
	/* disable i2c interrupt */
	task_disable_irq(i2c_ctrl_regs[port].irq);
	pd->task_waiting = TASK_ID_INVALID;

	/* Handle timeout */
	if (events & TASK_EVENT_TIMER) {
		pd->err = EC_ERROR_TIMEOUT;
		/* reset i2c port */
		i2c_reset(port, I2C_RC_TIMEOUT);
	}

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
	return IT83XX_SMB_SMBPCTL(port) & 0x03;
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

static void i2c_freq_changed(void)
{
	int i, f;

	for (i = 0; i < i2c_ports_used; i++) {
		for (f = ARRAY_SIZE(i2c_freq_select) - 1; f >= 0; f--) {
			if (i2c_ports[i].kbps >= i2c_freq_select[f].kpbs) {
				IT83XX_SMB_SCLKTS(i2c_ports[i].port) =
					i2c_freq_select[f].freq_set;
				break;
			}
		}
	}

	/* This field defines the SMCLK0/1/2 clock/data low timeout. */
	IT83XX_SMB_25MS = I2C_CLK_LOW_TIMEOUT;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

static void i2c_init(void)
{
	int i, p;

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
		/*
		 * bit0, The SMBus host interface is enabled.
		 * bit1, Enable to communicate with I2C device and
		 *       support I2C-compatible cycles.
		 * bit4, This bit controls the reset mechanism of SMBus master
		 *       to handle the SMDAT line low if 25ms reg timeout.
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

		pdata[i].task_waiting = TASK_ID_INVALID;
	}

	i2c_freq_changed();

	for (i = 0; i < I2C_PORT_COUNT; i++) {

		/* Use default timeout */
		i2c_set_timeout(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);
