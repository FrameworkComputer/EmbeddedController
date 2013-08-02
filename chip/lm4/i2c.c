/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for Chrome EC */

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
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

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

static task_id_t task_waiting_on_port[I2C_PORT_COUNT];

/**
 * Wait for port to go idle
 *
 * @param port		Port to check
 * @return EC_SUCCESS if port is idle; non-zero if error.
 */
static int wait_idle(int port)
{
	int i;
	int event = 0;

	i = LM4_I2C_MCS(port);
	while (i & LM4_I2C_MCS_BUSY) {
		/* Port is busy, so wait for the interrupt */
		task_waiting_on_port[port] = task_get_current();
		LM4_I2C_MIMR(port) = 0x03;
		/*
		 * We want to wait here quietly until the I2C interrupt comes
		 * along, but we don't want to lose any pending events that
		 * will be needed by the task that started the I2C transaction
		 * in the first place. So we save them up and restore them when
		 * the I2C is either completed or timed out. Refer to the
		 * implementation of usleep() for a similar situation.
		 */
		event |= (task_wait_event(SECOND) & ~TASK_EVENT_I2C_IDLE);
		LM4_I2C_MIMR(port) = 0x00;
		task_waiting_on_port[port] = TASK_ID_INVALID;
		if (event & TASK_EVENT_TIMER) {
			/* Restore any events that we saw while waiting */
			task_set_event(task_get_current(),
				       (event & ~TASK_EVENT_TIMER), 0);
			return EC_ERROR_TIMEOUT;
		}

		i = LM4_I2C_MCS(port);
	}

	/*
	 * Restore any events that we saw while waiting. TASK_EVENT_TIMER isn't
	 * one, because we've handled it above.
	 */
	task_set_event(task_get_current(), event, 0);

	/* Check for errors */
	if (i & (LM4_I2C_MCS_CLKTO | LM4_I2C_MCS_ARBLST | LM4_I2C_MCS_ERROR))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int i2c_get_line_levels(int port)
{
	/* Conveniently, MBMON bit (1 << 1) is SDA and (1 << 0) is SCL. */
	return LM4_I2C_MBMON(port) & 0x03;
}

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags)
{
	int rv, i;
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	uint32_t reg_mcs;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	reg_mcs = LM4_I2C_MCS(port);
	if (!started && (reg_mcs & (LM4_I2C_MCS_CLKTO | LM4_I2C_MCS_ARBLST))) {
		uint32_t tpr = LM4_I2C_MTPR(port);

		CPRINTF("[%T I2C%d bad status 0x%02x]\n", port, reg_mcs);

		/* Clock timeout or arbitration lost.  Reset port to clear. */
		LM4_SYSTEM_SRI2C |= (1 << port);
		clock_wait_cycles(3);
		LM4_SYSTEM_SRI2C &= ~(1 << port);
		clock_wait_cycles(3);

		/* Restore settings */
		LM4_I2C_MCR(port) = 0x10;
		LM4_I2C_MTPR(port) = tpr;

		/*
		 * We don't know what edges the slave saw, so sleep long enough
		 * that the slave will see the new start condition below.
		 */
		usleep(1000);
	}

	if (out) {
		LM4_I2C_MSA(port) = slave_addr & 0xff;
		for (i = 0; i < out_size; i++) {
			LM4_I2C_MDR(port) = out[i];
			/*
			 * Set up master control/status register
			 * MCS sequence on multi-byte write:
			 *     0x3 0x1 0x1 ... 0x1 0x5
			 * Single byte write:
			 *     0x7
			 */
			reg_mcs = LM4_I2C_MCS_RUN;
			/* Set start bit on first byte */
			if (!started) {
				started = 1;
				reg_mcs |= LM4_I2C_MCS_START;
			}
			/*
			 * Send stop bit if the stop flag is on, and caller
			 * doesn't expect to receive data.
			 */
			if ((flags & I2C_XFER_STOP) && in_size == 0 &&
			    i == (out_size - 1))
				reg_mcs |= LM4_I2C_MCS_STOP;

			LM4_I2C_MCS(port) = reg_mcs;

			rv = wait_idle(port);
			if (rv) {
				LM4_I2C_MCS(port) = LM4_I2C_MCS_STOP;
				return rv;
			}
		}
	}

	if (in_size) {
		if (out_size)
			/* resend start bit when change direction */
			started = 0;

		LM4_I2C_MSA(port) = (slave_addr & 0xff) | 0x01;
		for (i = 0; i < in_size; i++) {
			LM4_I2C_MDR(port) = in[i];
			/*
			 * MCS receive sequence on multi-byte read:
			 *     0xb 0x9 0x9 ... 0x9 0x5
			 * Single byte read:
			 *     0x7
			 */
			reg_mcs = LM4_I2C_MCS_RUN;
			if (!started) {
				started = 1;
				reg_mcs |= LM4_I2C_MCS_START;
			}
			/* ACK all bytes except the last one */
			if ((flags & I2C_XFER_STOP) && i == (in_size - 1))
				reg_mcs |= LM4_I2C_MCS_STOP;
			else
				reg_mcs |= LM4_I2C_MCS_ACK;

			LM4_I2C_MCS(port) = reg_mcs;
			rv = wait_idle(port);
			if (rv) {
				LM4_I2C_MCS(port) = LM4_I2C_MCS_STOP;
				return rv;
			}
			in[i] = LM4_I2C_MDR(port) & 0xff;
		}
	}

	/* Check for error conditions */
	if (LM4_I2C_MCS(port) & (LM4_I2C_MCS_CLKTO | LM4_I2C_MCS_ARBLST |
				 LM4_I2C_MCS_ERROR))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
		    int len)
{
	int rv;
	uint8_t reg, block_length;

	i2c_lock(port, 1);

	reg = offset;
	/*
	 * Send device reg space offset, and read back block length.  Keep this
	 * session open without a stop.
	 */
	rv = i2c_xfer(port, slave_addr, &reg, 1, &block_length, 1,
		      I2C_XFER_START);
	if (rv)
		goto exit;

	if (len && block_length > (len - 1))
		block_length = len - 1;

	rv = i2c_xfer(port, slave_addr, 0, 0, data, block_length,
		      I2C_XFER_STOP);
	data[block_length] = 0;

exit:
	i2c_lock(port, 0);
	return rv;
}

/*****************************************************************************/
/* Hooks */

static void i2c_freq_changed(void)
{
	int freq = clock_get_freq();
	int i;

	for (i = 0; i < I2C_PORTS_USED; i++) {
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
		CPRINTF("[%T I2C%d clk=%d tpr=%d freq=%d]\n",
			i2c_ports[i].port, freq, tpr, f);
#endif

		LM4_I2C_MTPR(i2c_ports[i].port) = tpr;
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT + 1);

static void i2c_init(void)
{
	uint32_t mask = 0;
	int i;

	/* Enable I2C modules and delay a few clocks */
	for (i = 0; i < I2C_PORTS_USED; i++)
		mask |= 1 << i2c_ports[i].port;

	LM4_SYSTEM_RCGCI2C |= mask;
	clock_wait_cycles(3);

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* No tasks are waiting on ports */
	for (i = 0; i < I2C_PORT_COUNT; i++)
		task_waiting_on_port[i] = TASK_ID_INVALID;

	/* Initialize ports as master, with interrupts enabled */
	for (i = 0; i < I2C_PORTS_USED; i++)
		LM4_I2C_MCR(i2c_ports[i].port) = 0x10;

	/* Set initial clock frequency */
	i2c_freq_changed();

	/* Enable irqs */
	task_enable_irq(LM4_IRQ_I2C0);
	task_enable_irq(LM4_IRQ_I2C1);
	task_enable_irq(LM4_IRQ_I2C2);
	task_enable_irq(LM4_IRQ_I2C3);
	task_enable_irq(LM4_IRQ_I2C4);
	task_enable_irq(LM4_IRQ_I2C5);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);

/**
 * Handle an interrupt on the specified port.
 *
 * @param port		I2C port generating interrupt
 */
static void handle_interrupt(int port)
{
	int id = task_waiting_on_port[port];

	/* Clear the interrupt status */
	LM4_I2C_MICR(port) = LM4_I2C_MMIS(port);

	/* Wake up the task which was waiting on the I2C interrupt, if any. */
	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_I2C_IDLE, 0);
}

static void i2c0_interrupt(void) { handle_interrupt(0); }
static void i2c1_interrupt(void) { handle_interrupt(1); }
static void i2c2_interrupt(void) { handle_interrupt(2); }
static void i2c3_interrupt(void) { handle_interrupt(3); }
static void i2c4_interrupt(void) { handle_interrupt(4); }
static void i2c5_interrupt(void) { handle_interrupt(5); }

DECLARE_IRQ(LM4_IRQ_I2C0, i2c0_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C1, i2c1_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C2, i2c2_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C3, i2c3_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C4, i2c4_interrupt, 2);
DECLARE_IRQ(LM4_IRQ_I2C5, i2c5_interrupt, 2);

/*****************************************************************************/
/* Console commands */

static int command_i2cread(int argc, char **argv)
{
	int port, addr, count = 1;
	char *e;
	int rv;
	int d, i;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	for (i = 0; i < I2C_PORTS_USED && port != i2c_ports[i].port; i++)
		;
	if (i >= I2C_PORTS_USED)
		return EC_ERROR_PARAM1;

	addr = strtoi(argv[2], &e, 0);
	if (*e || (addr & 0x01))
		return EC_ERROR_PARAM2;

	if (argc > 3) {
		count = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
	}

	ccprintf("Reading %d bytes from %d:0x%02x:", count, port, addr);
	i2c_lock(port, 1);
	LM4_I2C_MSA(port) = addr | 0x01;
	for (i = 0; i < count; i++) {
		if (i == 0)
			LM4_I2C_MCS(port) = (count > 1 ? 0x0b : 0x07);
		else
			LM4_I2C_MCS(port) = (i == count - 1 ? 0x05 : 0x09);
		rv = wait_idle(port);
		if (rv != EC_SUCCESS) {
			LM4_I2C_MCS(port) = LM4_I2C_MCS_STOP;
			i2c_lock(port, 0);
			return rv;
		}
		d = LM4_I2C_MDR(port) & 0xff;
		ccprintf(" 0x%02x", d);
	}
	i2c_lock(port, 0);
	ccputs("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cread, command_i2cread,
			"port addr [count]",
			"Read from I2C",
			NULL);
