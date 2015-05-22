/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for Chrome EC */

#include "atomic.h"
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

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* Flags for writes to MCS */
#define LM4_I2C_MCS_RUN   (1 << 0)
#define LM4_I2C_MCS_START (1 << 1)
#define LM4_I2C_MCS_STOP  (1 << 2)
#define LM4_I2C_MCS_ACK   (1 << 3)
#define LM4_I2C_MCS_HS    (1 << 4)
#define LM4_I2C_MCS_QCMD  (1 << 5)

/* Flags for reads from MCS */
#define LM4_I2C_MCS_BUSY   (1 << 0)
#define LM4_I2C_MCS_ERROR  (1 << 1)
#define LM4_I2C_MCS_ADRACK (1 << 2)
#define LM4_I2C_MCS_DATACK (1 << 3)
#define LM4_I2C_MCS_ARBLST (1 << 4)
#define LM4_I2C_MCS_IDLE   (1 << 5)
#define LM4_I2C_MCS_BUSBSY (1 << 6)
#define LM4_I2C_MCS_CLKTO  (1 << 7)

/*
 * Minimum delay between resetting the port or sending a stop condition, and
 * when the port can be expected to be back in an idle state (and the slave
 * has had long enough to see the start/stop condition edges).
 *
 * 500 us = 50 clocks at 100 KHz bus speed.  This has been experimentally
 * determined to be enough.
 */
#define I2C_IDLE_US 500

/* Default maximum time we allow for an I2C transfer */
#define I2C_TIMEOUT_DEFAULT_US (100 * MSEC)

/* IRQ for each port */
static const uint32_t i2c_irqs[] = {LM4_IRQ_I2C0, LM4_IRQ_I2C1, LM4_IRQ_I2C2,
				    LM4_IRQ_I2C3, LM4_IRQ_I2C4, LM4_IRQ_I2C5};
BUILD_ASSERT(ARRAY_SIZE(i2c_irqs) == I2C_PORT_COUNT);

/* I2C port state data */
struct i2c_port_data {
	const uint8_t *out;	/* Output data pointer */
	int out_size;		/* Output data to transfer, in bytes */
	uint8_t *in;		/* Input data pointer */
	int in_size;		/* Input data to transfer, in bytes */
	int flags;		/* Flags (I2C_XFER_*) */
	int idx;		/* Index into input/output data */
	int err;		/* Error code, if any */
	uint32_t timeout_us;	/* Transaction timeout, or 0 to use default */

	/* Task waiting on port, or TASK_ID_INVALID if none. */
	int task_waiting;
};
static struct i2c_port_data pdata[I2C_PORT_COUNT];

int i2c_is_busy(int port)
{
	return LM4_I2C_MCS(port) & LM4_I2C_MCS_BUSBSY;
}

/**
 * I2C transfer engine.
 *
 * @return Zero when done with transfer (ready to wake task).
 *
 * MCS sequence on multi-byte write:
 *     0x3 0x1 0x1 ... 0x1 0x5
 * Single byte write:
 *     0x7
 *
 * MCS receive sequence on multi-byte read:
 *     0xb 0x9 0x9 ... 0x9 0x5
 * Single byte read:
 *     0x7
 */
int i2c_do_work(int port)
{
	struct i2c_port_data *pd = pdata + port;
	uint32_t reg_mcs = LM4_I2C_MCS_RUN;

	if (pd->flags & I2C_XFER_START) {
		/* Set start bit on first byte */
		reg_mcs |= LM4_I2C_MCS_START;
		pd->flags &= ~I2C_XFER_START;
	} else if (LM4_I2C_MCS(port) & (LM4_I2C_MCS_CLKTO | LM4_I2C_MCS_ARBLST |
					LM4_I2C_MCS_ERROR)) {
		/*
		 * Error after starting; abort transfer.  Ignore errors at
		 * start because arbitration and timeout errors are taken care
		 * of in i2c_xfer(), and slave ack failures will automatically
		 * clear once we send a start condition.
		 */
		pd->err = EC_ERROR_UNKNOWN;
		return 0;
	}

	if (pd->out_size) {
		/* Send next byte of output */
		LM4_I2C_MDR(port) = *(pd->out++);
		pd->idx++;

		/* Handle starting to send last byte */
		if (pd->idx == pd->out_size) {

			/* Done with output after this */
			pd->out_size = 0;
			pd->idx = 0;

			/* Resend start bit when changing direction */
			pd->flags |= I2C_XFER_START;

			/*
			 * Send stop bit after last byte if the stop flag is
			 * on, and caller doesn't expect to receive data.
			 */
			if ((pd->flags & I2C_XFER_STOP) && pd->in_size == 0)
				reg_mcs |= LM4_I2C_MCS_STOP;
		}

		LM4_I2C_MCS(port) = reg_mcs;
		return 1;

	} else if (pd->in_size) {
		if (pd->idx) {
			/* Copy the byte we just read */
			*(pd->in++) = LM4_I2C_MDR(port) & 0xff;
		} else {
			/* Starting receive; switch to receive address */
			LM4_I2C_MSA(port) |= 0x01;
		}

		if (pd->idx < pd->in_size) {
			/* More data to read */
			pd->idx++;

			/* ACK all bytes except the last one */
			if ((pd->flags & I2C_XFER_STOP) &&
			    pd->idx == pd->in_size)
				reg_mcs |= LM4_I2C_MCS_STOP;
			else
				reg_mcs |= LM4_I2C_MCS_ACK;

			LM4_I2C_MCS(port) = reg_mcs;
			return 1;
		}
	}

	/* If we're still here, done with transfer */
	return 0;
}

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags)
{
	struct i2c_port_data *pd = pdata + port;
	uint32_t reg_mcs = LM4_I2C_MCS(port);
	int events = 0;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	/* Copy data to port struct */
	pd->out = out;
	pd->out_size = out_size;
	pd->in = in;
	pd->in_size = in_size;
	pd->flags = flags;
	pd->idx = 0;
	pd->err = 0;

	/* Make sure we're in a good state to start */
	if ((flags & I2C_XFER_START) &&
	    ((reg_mcs & (LM4_I2C_MCS_CLKTO | LM4_I2C_MCS_ARBLST)) ||
			    (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {
		uint32_t tpr = LM4_I2C_MTPR(port);

		CPRINTS("I2C%d Addr:%02X bad status 0x%02x, SCL=%d, SDA=%d",
				port,
				slave_addr,
				reg_mcs,
				i2c_get_line_levels(port) & I2C_LINE_SCL_HIGH,
				i2c_get_line_levels(port) & I2C_LINE_SDA_HIGH);

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);

		/* Clock timeout or arbitration lost.  Reset port to clear. */
		atomic_or(LM4_SYSTEM_SRI2C_ADDR, (1 << port));
		clock_wait_cycles(3);
		atomic_clear(LM4_SYSTEM_SRI2C_ADDR, (1 << port));
		clock_wait_cycles(3);

		/* Restore settings */
		LM4_I2C_MCR(port) = 0x10;
		LM4_I2C_MTPR(port) = tpr;

		/*
		 * We don't know what edges the slave saw, so sleep long enough
		 * that the slave will see the new start condition below.
		 */
		usleep(I2C_IDLE_US);
	}

	/* Set slave address for transmit */
	LM4_I2C_MSA(port) = slave_addr & 0xff;

	/* Enable interrupts */
	pd->task_waiting = task_get_current();
	LM4_I2C_MICR(port) = 0x03;
	LM4_I2C_MIMR(port) = 0x03;

	/* Kick the port interrupt handler to start the transfer */
	task_trigger_irq(i2c_irqs[port]);

	/* Wait for transfer complete or timeout */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE, pd->timeout_us);

	/* Disable interrupts */
	LM4_I2C_MIMR(port) = 0x00;
	pd->task_waiting = TASK_ID_INVALID;

	/* Handle timeout */
	if (events & TASK_EVENT_TIMER)
		pd->err = EC_ERROR_TIMEOUT;

	if (pd->err) {
		/* Force port back idle */
		LM4_I2C_MCS(port) = LM4_I2C_MCS_STOP;
		usleep(I2C_IDLE_US);
	}

	return pd->err;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;
	int ret;

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	if (get_scl_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;

	/* If we are driving the pin low, it must be low. */
	if (gpio_get_level(g) == 0)
		return 0;

	/*
	 * Otherwise, we need to toggle it to an input to read the true pin
	 * state.
	 */
	gpio_set_flags(g, GPIO_INPUT);
	ret = gpio_get_level(g);
	gpio_set_flags(g, GPIO_ODR_HIGH);

	return ret;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;
	int ret;

	/* If no SDA pin defined for this port, then return 1 to appear idle. */
	if (get_sda_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;

	/* If we are driving the pin low, it must be low. */
	if (gpio_get_level(g) == 0)
		return 0;

	/*
	 * Otherwise, we need to toggle it to an input to read the true pin
	 * state.
	 */
	gpio_set_flags(g, GPIO_INPUT);
	ret = gpio_get_level(g);
	gpio_set_flags(g, GPIO_ODR_HIGH);

	return ret;
}

int i2c_get_line_levels(int port)
{
	/* Conveniently, MBMON bit (1 << 1) is SDA and (1 << 0) is SCL. */
	return LM4_I2C_MBMON(port) & 0x03;
}

void i2c_set_timeout(int port, uint32_t timeout)
{
	pdata[port].timeout_us = timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

/*****************************************************************************/
/* Hooks */

static void i2c_freq_changed(void)
{
	int freq = clock_get_freq();
	int i;

	for (i = 0; i < i2c_ports_used; i++) {
		/*
		 * From datasheet:
		 *     SCL_PRD = 2 * (1 + TPR) * (SCL_LP + SCL_HP) * CLK_PRD
		 *
		 * so:
		 *     TPR = SCL_PRD / (2 * (SCL_LP + SCL_HP) * CLK_PRD) - 1
		 *
		 * converting from period to frequency:
		 *     TPR = CLK_FREQ / (SCL_FREQ * 2 * (SCL_LP + SCL_HP)) - 1
		 */
		const int d = 2 * (6 + 4) * (i2c_ports[i].kbps * 1000);

		/* Round TPR up, so desired kbps is an upper bound */
		const int tpr = (freq + d - 1) / d - 1;

#ifdef PRINT_I2C_SPEEDS
		const int f = freq / (2 * (1 + tpr) * (6 + 4));
		CPRINTS("I2C%d clk=%d tpr=%d freq=%d",
			i2c_ports[i].port, freq, tpr, f);
#endif

		LM4_I2C_MTPR(i2c_ports[i].port) = tpr;
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

static void i2c_init(void)
{
	uint32_t mask = 0;
	int i;

	/* Enable I2C modules in run and sleep modes. */
	for (i = 0; i < i2c_ports_used; i++)
		mask |= 1 << i2c_ports[i].port;

	clock_enable_peripheral(CGC_OFFSET_I2C, mask,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Initialize ports as master, with interrupts enabled */
	for (i = 0; i < i2c_ports_used; i++)
		LM4_I2C_MCR(i2c_ports[i].port) = 0x10;

	/* Set initial clock frequency */
	i2c_freq_changed();

	/* Enable IRQs; no tasks are waiting on ports */
	for (i = 0; i < I2C_PORT_COUNT; i++) {
		pdata[i].task_waiting = TASK_ID_INVALID;
		task_enable_irq(i2c_irqs[i]);

		/* Use default timeout */
		i2c_set_timeout(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);

/**
 * Handle an interrupt on the specified port.
 *
 * @param port		I2C port generating interrupt
 */
static void handle_interrupt(int port)
{
	int id = pdata[port].task_waiting;

	/* Clear the interrupt status */
	LM4_I2C_MICR(port) = LM4_I2C_MMIS(port);

	/* If no task is waiting, just return */
	if (id == TASK_ID_INVALID)
		return;

	/* If done doing work, wake up the task waiting for the transfer */
	if (!i2c_do_work(port))
		task_set_event(id, TASK_EVENT_I2C_IDLE, 0);
}

void i2c0_interrupt(void) { handle_interrupt(0); }
void i2c1_interrupt(void) { handle_interrupt(1); }
void i2c2_interrupt(void) { handle_interrupt(2); }
void i2c3_interrupt(void) { handle_interrupt(3); }
void i2c4_interrupt(void) { handle_interrupt(4); }
void i2c5_interrupt(void) { handle_interrupt(5); }

DECLARE_IRQ(LM4_IRQ_I2C0, i2c0_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C1, i2c1_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C2, i2c2_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C3, i2c3_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C4, i2c4_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C5, i2c5_interrupt, 2);
