/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for MEC1322 */

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

#define I2C_CLOCK 16000000 /* 16 MHz */

/* Status */
#define STS_NBB (1 << 0) /* Bus busy */
#define STS_LAB (1 << 1) /* Arbitration lost */
#define STS_LRB (1 << 3) /* Last received bit */
#define STS_BER (1 << 4) /* Bus error */
#define STS_PIN (1 << 7) /* Pending interrupt */

/* Control */
#define CTRL_ACK (1 << 0) /* Acknowledge */
#define CTRL_STO (1 << 1) /* STOP */
#define CTRL_STA (1 << 2) /* START */
#define CTRL_ENI (1 << 3) /* Enable interrupt */
#define CTRL_ESO (1 << 6) /* Enable serial output */
#define CTRL_PIN (1 << 7) /* Pending interrupt not */

static task_id_t task_waiting_on_port[I2C_PORT_COUNT];

static void configure_port_speed(int port, int kbps)
{
	int min_t_low, min_t_high;
	int t_low, t_high;
	const int period = I2C_CLOCK / 1000 / kbps;

	/*
	 * Refer to NXP UM10204 for minimum timing requirement of T_Low and
	 * T_High.
	 * http://www.nxp.com/documents/user_manual/UM10204.pdf
	 */

	if (kbps > 400) {
		/* Fast mode plus */
		min_t_low = I2C_CLOCK * 0.5 / 1000000 - 1; /* 0.5 us */
		min_t_high = I2C_CLOCK * 0.26 / 1000000 - 1; /* 0.26 us */
		MEC1322_I2C_DATA_TIM(port) = 0x06060601;
		MEC1322_I2C_DATA_TIM_2(port) = 0x06;
	} else if (kbps > 100) {
		/* Fast mode */
		min_t_low = I2C_CLOCK * 1.3 / 1000000 - 1; /* 1.3 us */
		min_t_high = I2C_CLOCK * 0.6 / 1000000 - 1; /* 0.6 us */
		MEC1322_I2C_DATA_TIM(port) = 0x040a0a01;
		MEC1322_I2C_DATA_TIM_2(port) = 0x0a;
	} else {
		/* Standard mode */
		min_t_low = I2C_CLOCK * 4.7 / 1000000 - 1; /* 4.7 us */
		min_t_high = I2C_CLOCK * 4.0 / 1000000 - 1; /* 4.0 us */
		MEC1322_I2C_DATA_TIM(port) = 0x0c4d5006;
		MEC1322_I2C_DATA_TIM_2(port) = 0x4d;
	}

	t_low = MAX(min_t_low + 1, period / 2);
	t_high = MAX(min_t_high + 1, period - t_low);

	MEC1322_I2C_BUS_CLK(port) = ((t_high & 0xff) << 8) |
				    (t_low & 0xff);
}

static void configure_port(int port, int kbps)
{
	MEC1322_I2C_CTRL(port) = CTRL_PIN;
	MEC1322_I2C_OWN_ADDR(port) = 0x0;
	configure_port_speed(port, kbps);
	MEC1322_I2C_CTRL(port) = CTRL_PIN | CTRL_ESO | CTRL_ACK | CTRL_ENI;
	MEC1322_I2C_CONFIG(port) |= 1 << 10; /* ENAB */

	/* Enable interrupt */
	MEC1322_I2C_CONFIG(port) |= 1 << 29; /* ENIDI */
	MEC1322_INT_ENABLE(12) |= (1 << port);
	MEC1322_INT_BLK_EN |= 1 << 12;
	task_enable_irq(MEC1322_IRQ_I2C_0 + port);
}

static void reset_port(int port)
{
	int i;

	MEC1322_I2C_CONFIG(port) |= 1 << 9;
	udelay(100);
	MEC1322_I2C_CONFIG(port) &= ~(1 << 9);

	for (i = 0; i < i2c_ports_used; ++i)
		if (port == i2c_ports[i].port) {
			configure_port(i2c_ports[i].port, i2c_ports[i].kbps);
			break;
		}
}

static int wait_for_interrupt(int port, int *event)
{
	task_waiting_on_port[port] = task_get_current();
	task_enable_irq(MEC1322_IRQ_I2C_0 + port);
	/*
	 * We want to wait here quietly until the I2C interrupt comes
	 * along, but we don't want to lose any pending events that
	 * will be needed by the task that started the I2C transaction
	 * in the first place. So we save them up and restore them when
	 * the I2C is either completed or timed out. Refer to the
	 * implementation of usleep() for a similar situation.
	 */
	*event |= (task_wait_event(SECOND) & ~TASK_EVENT_I2C_IDLE);
	task_waiting_on_port[port] = TASK_ID_INVALID;
	if (*event & TASK_EVENT_TIMER) {
		/* Restore any events that we saw while waiting */
		task_set_event(task_get_current(),
				(*event & ~TASK_EVENT_TIMER), 0);
		return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}

static int wait_idle(int port)
{
	uint8_t sts = MEC1322_I2C_STATUS(port);
	int rv;
	int event = 0;

	while (!(sts & STS_NBB)) {
		rv = wait_for_interrupt(port, &event);
		if (rv)
			return rv;
		sts = MEC1322_I2C_STATUS(port);
	}
	/*
	 * Restore any events that we saw while waiting. TASK_EVENT_TIMER isn't
	 * one, because we've handled it above.
	 */
	task_set_event(task_get_current(), event, 0);

	if (sts & (STS_BER | STS_LAB))
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

static int wait_byte_done(int port)
{
	uint8_t sts = MEC1322_I2C_STATUS(port);
	int rv;
	int event = 0;

	while (sts & STS_PIN) {
		rv = wait_for_interrupt(port, &event);
		if (rv)
			return rv;
		sts = MEC1322_I2C_STATUS(port);
	}
	/*
	 * Restore any events that we saw while waiting. TASK_EVENT_TIMER isn't
	 * one, because we've handled it above.
	 */
	task_set_event(task_get_current(), event, 0);

	return sts & STS_LRB;
}

static inline void fill_in_buf(uint8_t *in, int id, uint8_t val)
{
	/*
	 * On MEC1322, first byte read is dummy read (slave addr).
	 * Throw it away.
	 */
	if (id != 0)
		in[id - 1] = val;
}

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags)
{
	int i;
	int started = (flags & I2C_XFER_START) ? 0 : 1;
	uint8_t reg_sts;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	wait_idle(port);

	reg_sts = MEC1322_I2C_STATUS(port);
	if (!started &&
	    (((reg_sts & (STS_BER | STS_LAB)) || !(reg_sts & STS_NBB)) ||
			    (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {
		CPRINTS("I2C%d bad status 0x%02x, SCL=%d, SDA=%d", port,
			reg_sts,
			i2c_get_line_levels(port) & I2C_LINE_SCL_HIGH,
			i2c_get_line_levels(port) & I2C_LINE_SDA_HIGH);

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);

		/* Bus error, bus busy, or arbitration lost. Reset port. */
		reset_port(port);

		/*
		 * We don't know what edges the slave saw, so sleep long enough
		 * that the slave will see the new start condition below.
		 */
		usleep(1000);
	}

	if (out) {
		MEC1322_I2C_DATA(port) = (uint8_t)slave_addr;

		/*
		 * Clock out the slave address. Send START bit if start flag is
		 * set.
		 */
		MEC1322_I2C_CTRL(port) = CTRL_PIN | CTRL_ESO | CTRL_ENI |
					 CTRL_ACK | (started ? 0 : CTRL_STA);
		if (!started)
			started = 1;

		for (i = 0; i < out_size; ++i) {
			if (wait_byte_done(port))
				goto err_i2c_xfer;
			MEC1322_I2C_DATA(port) = out[i];
		}
		if (wait_byte_done(port))
			goto err_i2c_xfer;

		/*
		 * Send STOP bit if the stop flag is on, and caller
		 * doesn't expect to receive data.
		 */
		if ((flags & I2C_XFER_STOP) && in_size == 0) {
			MEC1322_I2C_CTRL(port) = CTRL_PIN | CTRL_ESO |
						 CTRL_STO | CTRL_ACK;
		}
	}

	if (in_size) {
		if (out_size) {
			/* resend start bit when change direction */
			MEC1322_I2C_CTRL(port) = CTRL_ESO | CTRL_STA |
						 CTRL_ACK | CTRL_ENI;
		}

		MEC1322_I2C_DATA(port) = (uint8_t)slave_addr | 0x01;

		if (!started) {
			started = 1;
			/* Clock out slave address with START bit */
			MEC1322_I2C_CTRL(port) = CTRL_PIN | CTRL_ESO |
						 CTRL_STA | CTRL_ACK | CTRL_ENI;
		}

		/* On MEC1322, first byte read is dummy read (slave addr) */
		in_size++;

		for (i = 0; i < in_size - 2; ++i) {
			if (wait_byte_done(port))
				goto err_i2c_xfer;
			fill_in_buf(in, i, MEC1322_I2C_DATA(port));
		}
		if (wait_byte_done(port))
			goto err_i2c_xfer;

		/*
		 * De-assert ACK bit before reading the next to last byte,
		 * so that the last byte is NACK'ed.
		 */
		MEC1322_I2C_CTRL(port) = CTRL_ESO | CTRL_ENI;
		fill_in_buf(in, in_size - 2, MEC1322_I2C_DATA(port));
		if (wait_byte_done(port))
			goto err_i2c_xfer;

		/* Send STOP if stop flag is set */
		MEC1322_I2C_CTRL(port) =
			CTRL_PIN | CTRL_ESO | CTRL_ACK |
			((flags & I2C_XFER_STOP) ? CTRL_STO : 0);

		/* Now read the last byte */
		fill_in_buf(in, in_size - 1, MEC1322_I2C_DATA(port));
	}

	/* Check for error conditions */
	if (MEC1322_I2C_STATUS(port) & (STS_LAB | STS_BER))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
err_i2c_xfer:
	/* Send STOP and return error */
	MEC1322_I2C_CTRL(port) = CTRL_PIN | CTRL_ESO | CTRL_STO | CTRL_ACK;
	return EC_ERROR_UNKNOWN;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	if (get_scl_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;

	return gpio_get_level(g);
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	/* If no SDA pin defined for this port, then return 1 to appear idle. */
	if (get_sda_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;

	return gpio_get_level(g);
}

int i2c_get_line_levels(int port)
{
	return (MEC1322_I2C_BB_CTRL(port) >> 5) & 0x3;
}

static void i2c_init(void)
{
	int i;

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	for (i = 0; i < i2c_ports_used; ++i)
		configure_port(i2c_ports[i].port, i2c_ports[i].kbps);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);

static void handle_interrupt(int port)
{
	int id = task_waiting_on_port[port];

	/* Clear the interrupt status */
	MEC1322_I2C_COMPLETE(port) |= 1 << 29;

	/*
	 * Write to control register interferes with I2C transaction.
	 * Instead, let's disable IRQ from the core until the next time
	 * we want to wait for STS_PIN/STS_NBB.
	 */
	task_disable_irq(MEC1322_IRQ_I2C_0 + port);

	/* Wake up the task which was waiting on the I2C interrupt, if any. */
	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_I2C_IDLE, 0);
}

void i2c0_interrupt(void) { handle_interrupt(0); }
void i2c1_interrupt(void) { handle_interrupt(1); }
void i2c2_interrupt(void) { handle_interrupt(2); }
void i2c3_interrupt(void) { handle_interrupt(3); }

DECLARE_IRQ(MEC1322_IRQ_I2C_0, i2c0_interrupt, 2);
DECLARE_IRQ(MEC1322_IRQ_I2C_1, i2c1_interrupt, 2);
DECLARE_IRQ(MEC1322_IRQ_I2C_2, i2c2_interrupt, 2);
DECLARE_IRQ(MEC1322_IRQ_I2C_3, i2c3_interrupt, 2);
