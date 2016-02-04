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

/* Completion */
#define COMP_IDLE (1 << 29)    /* i2c bus is idle */
#define COMP_RW_BITS_MASK 0x3C /* R/W bits mask */

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK_SIZE 32

/*
 * Amount of time to blocking wait for i2c bus to finish. After this blocking
 * timeout, if the bus is still not finished, then allow other tasks to run.
 * Note: this is just long enough for a 400kHz bus to finish transmitting one
 * byte assuming the bus isn't being held.
 */
#define I2C_WAIT_BLOCKING_TIMEOUT_US 25

enum i2c_transaction_state {
	/* Stop condition was sent in previous transaction */
	I2C_TRANSACTION_STOPPED,
	/* Stop condition was not sent in previous transaction */
	I2C_TRANSACTION_OPEN,
};

/* I2C controller state data */
static struct {
	/* Transaction timeout, or 0 to use default. */
	uint32_t timeout_us;
	/* Task waiting on port, or TASK_ID_INVALID if none. */
	volatile task_id_t task_waiting;
	enum i2c_transaction_state transaction_state;
} cdata[I2C_CONTROLLER_COUNT];

/* Map port number to port name in datasheet, for debug prints. */
static const char *i2c_port_names[MEC1322_I2C_PORT_COUNT] = {
	[MEC1322_I2C0_0] = "0_0",
	[MEC1322_I2C0_1] = "0_1",
	[MEC1322_I2C1] = "1",
	[MEC1322_I2C2] = "2",
	[MEC1322_I2C3] = "3",
};

static void configure_controller_speed(int controller, int kbps)
{
	int t_low, t_high;
	const int period = I2C_CLOCK / 1000 / kbps;

	/*
	 * Refer to NXP UM10204 for minimum timing requirement of T_Low and
	 * T_High.
	 * http://www.nxp.com/documents/user_manual/UM10204.pdf
	 */
	if (kbps > 400) {
		/* Fast mode plus */
		t_low = t_high = period / 2 - 1;
		MEC1322_I2C_DATA_TIM(controller) = 0x06060601;
		MEC1322_I2C_DATA_TIM_2(controller) = 0x06;
	} else if (kbps > 100) {
		/* Fast mode */
		/* By spec, clk low period is 1.3us min */
		t_low = MAX((int)(I2C_CLOCK * 1.3 / 1000000), period / 2 - 1);
		t_high = period - t_low - 2;
		MEC1322_I2C_DATA_TIM(controller) = 0x040a0a01;
		MEC1322_I2C_DATA_TIM_2(controller) = 0x0a;
	} else {
		/* Standard mode */
		t_low = t_high = period / 2 - 1;
		MEC1322_I2C_DATA_TIM(controller) = 0x0c4d5006;
		MEC1322_I2C_DATA_TIM_2(controller) = 0x4d;
	}

	/* Clock periods is one greater than the contents of these fields */
	MEC1322_I2C_BUS_CLK(controller) = ((t_high & 0xff) << 8) |
					  (t_low & 0xff);
}

static void configure_controller(int controller, int kbps)
{
	MEC1322_I2C_CTRL(controller) = CTRL_PIN;
	MEC1322_I2C_OWN_ADDR(controller) = 0x0;
	configure_controller_speed(controller, kbps);
	MEC1322_I2C_CTRL(controller) = CTRL_PIN | CTRL_ESO |
				       CTRL_ACK | CTRL_ENI;
	MEC1322_I2C_CONFIG(controller) |= 1 << 10; /* ENAB */

	/* Enable interrupt */
	MEC1322_I2C_CONFIG(controller) |= 1 << 29; /* ENIDI */
	MEC1322_INT_ENABLE(12) |= (1 << controller);
	MEC1322_INT_BLK_EN |= 1 << 12;
}

static void reset_controller(int controller)
{
	int i;

	MEC1322_I2C_CONFIG(controller) |= 1 << 9;
	udelay(100);
	MEC1322_I2C_CONFIG(controller) &= ~(1 << 9);

	for (i = 0; i < i2c_ports_used; ++i)
		if (controller == i2c_port_to_controller(i2c_ports[i].port)) {
			configure_controller(controller, i2c_ports[i].kbps);
			cdata[controller].transaction_state =
				I2C_TRANSACTION_STOPPED;
			break;
		}
}

static int wait_for_interrupt(int controller, int timeout)
{
	int event;

	if (timeout <= 0)
		return EC_ERROR_TIMEOUT;

	cdata[controller].task_waiting = task_get_current();
	task_enable_irq(MEC1322_IRQ_I2C_0 + controller);

	/* Wait until I2C interrupt or timeout. */
	event = task_wait_event_mask(TASK_EVENT_I2C_IDLE, timeout);

	task_disable_irq(MEC1322_IRQ_I2C_0 + controller);
	cdata[controller].task_waiting = TASK_ID_INVALID;

	return (event & TASK_EVENT_TIMER) ? EC_ERROR_TIMEOUT : EC_SUCCESS;
}

static int wait_idle(int controller)
{
	uint8_t sts = MEC1322_I2C_STATUS(controller);
	uint64_t block_timeout = get_time().val + I2C_WAIT_BLOCKING_TIMEOUT_US;
	uint64_t task_timeout = block_timeout + cdata[controller].timeout_us;
	int rv = 0;

	while (!(sts & STS_NBB)) {
		if (rv)
			return rv;
		if (get_time().val > block_timeout)
			rv = wait_for_interrupt(controller,
						task_timeout - get_time().val);
		sts = MEC1322_I2C_STATUS(controller);
	}

	if (sts & (STS_BER | STS_LAB))
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

static int wait_byte_done(int controller)
{
	uint8_t sts = MEC1322_I2C_STATUS(controller);
	uint64_t block_timeout = get_time().val + I2C_WAIT_BLOCKING_TIMEOUT_US;
	uint64_t task_timeout = block_timeout + cdata[controller].timeout_us;
	int rv = 0;

	while (sts & STS_PIN) {
		if (rv)
			return rv;
		if (get_time().val > block_timeout)
			rv = wait_for_interrupt(controller,
						task_timeout - get_time().val);
		sts = MEC1322_I2C_STATUS(controller);
	}

	return sts & STS_LRB;
}

static void select_port(int port)
{
	/*
	 * I2C0_1 uses port 1 of controller 0. All other I2C pin sets
	 * use port 0.
	 */
	uint8_t port_sel = (port == MEC1322_I2C0_1) ? 1 : 0;
	int controller = i2c_port_to_controller(port);

	MEC1322_I2C_CONFIG(controller) &= ~0xf;
	MEC1322_I2C_CONFIG(controller) |= port_sel;

}

static inline int get_line_level(int controller)
{
	int ret, ctrl;
	/*
	* We need to enable BB (Bit Bang) mode in order to read line level
	* properly, othervise line levels return always idle (0x60).
	*/
	ctrl = MEC1322_I2C_BB_CTRL(controller);
	MEC1322_I2C_BB_CTRL(controller) |= 1;
	ret = (MEC1322_I2C_BB_CTRL(controller) >> 5) & 0x3;
	MEC1322_I2C_BB_CTRL(controller) = ctrl;
	return ret;
}

static inline void push_in_buf(uint8_t **in, uint8_t val, int skip)
{
	if (!skip) {
		**in = val;
		(*in)++;
	}
}

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	int i;
	int controller;
	int send_start = flags & I2C_XFER_START;
	int send_stop = flags & I2C_XFER_STOP;
	int skip = 0;
	int bytes_to_read;
	uint8_t reg;
	int ret_done;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	select_port(port);
	controller = i2c_port_to_controller(port);
	if (send_start &&
	    cdata[controller].transaction_state == I2C_TRANSACTION_STOPPED)
		wait_idle(controller);

	reg = MEC1322_I2C_STATUS(controller);
	if (send_start &&
	    cdata[controller].transaction_state == I2C_TRANSACTION_STOPPED &&
	    (((reg & (STS_BER | STS_LAB)) || !(reg & STS_NBB)) ||
			    (get_line_level(controller)
			    != I2C_LINE_IDLE))) {
		CPRINTS("i2c%s bad status 0x%02x, SCL=%d, SDA=%d",
			i2c_port_names[port], reg,
			get_line_level(controller) & I2C_LINE_SCL_HIGH,
			get_line_level(controller) & I2C_LINE_SDA_HIGH);

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);

		/* Bus error, bus busy, or arbitration lost. Try reset. */
		reset_controller(controller);
		select_port(port);

		/*
		 * We don't know what edges the slave saw, so sleep long enough
		 * that the slave will see the new start condition below.
		 */
		usleep(1000);
	}

	if (out_size) {
		if (send_start) {
			MEC1322_I2C_DATA(controller) = (uint8_t)slave_addr;

			/* Clock out the slave address, sending START bit */
			MEC1322_I2C_CTRL(controller) = CTRL_PIN | CTRL_ESO |
						       CTRL_ENI | CTRL_ACK |
						       CTRL_STA;
			cdata[controller].transaction_state =
				I2C_TRANSACTION_OPEN;
		}

		for (i = 0; i < out_size; ++i) {
			ret_done = wait_byte_done(controller);
			if (ret_done)
				goto err_chip_i2c_xfer;
			MEC1322_I2C_DATA(controller) = out[i];
		}
		ret_done = wait_byte_done(controller);
		if (ret_done)
			goto err_chip_i2c_xfer;

		/*
		 * Send STOP bit if the stop flag is on, and caller
		 * doesn't expect to receive data.
		 */
		if (send_stop && in_size == 0) {
			MEC1322_I2C_CTRL(controller) = CTRL_PIN | CTRL_ESO |
						       CTRL_STO | CTRL_ACK;
			cdata[controller].transaction_state =
				I2C_TRANSACTION_STOPPED;
		}
	}

	if (in_size) {
		/* Resend start bit when changing direction */
		if (out_size || send_start) {
			/* Repeated start case */
			if (cdata[controller].transaction_state ==
			    I2C_TRANSACTION_OPEN)
				MEC1322_I2C_CTRL(controller) = CTRL_ESO |
							       CTRL_STA |
							       CTRL_ACK |
							       CTRL_ENI;

			MEC1322_I2C_DATA(controller) = (uint8_t)slave_addr
						     | 0x01;

			/* New transaction case, clock out slave address. */
			if (cdata[controller].transaction_state ==
			    I2C_TRANSACTION_STOPPED)
				MEC1322_I2C_CTRL(controller) = CTRL_ESO |
							       CTRL_STA |
							       CTRL_ACK |
							       CTRL_ENI |
							       CTRL_PIN;

			cdata[controller].transaction_state =
				I2C_TRANSACTION_OPEN;

			/* Skip over the dummy byte */
			skip = 1;
			in_size++;
		}

		/* Special flags need to be set for last two bytes */
		bytes_to_read = send_stop ? in_size - 2 : in_size;

		for (i = 0; i < bytes_to_read; ++i) {
			ret_done = wait_byte_done(controller);
			if (ret_done)
				goto err_chip_i2c_xfer;
			push_in_buf(&in, MEC1322_I2C_DATA(controller), skip);
			skip = 0;
		}
		ret_done = wait_byte_done(controller);
		if (ret_done)
			goto err_chip_i2c_xfer;

		if (send_stop) {
			/*
			 * De-assert ACK bit before reading the next to last
			 * byte, so that the last byte is NACK'ed.
			 */
			MEC1322_I2C_CTRL(controller) = CTRL_ESO | CTRL_ENI;
			push_in_buf(&in, MEC1322_I2C_DATA(controller), skip);
			ret_done = wait_byte_done(controller);
			if (ret_done)
				goto err_chip_i2c_xfer;

			/* Send STOP */
			MEC1322_I2C_CTRL(controller) =
				CTRL_PIN | CTRL_ESO | CTRL_ACK | CTRL_STO;

			cdata[controller].transaction_state =
				I2C_TRANSACTION_STOPPED;

			/*
			 * We need to know our stop point two bytes in
			 * advance. If we don't know soon enough, we need
			 * to do an extra dummy read (to last_addr + 1) to
			 * issue the stop.
			 */
			push_in_buf(&in, MEC1322_I2C_DATA(controller),
				    in_size == 1);
		}
	}

	/* Check for error conditions */
	if (MEC1322_I2C_STATUS(controller) & (STS_LAB | STS_BER))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
err_chip_i2c_xfer:
	/* Send STOP and return error */
	MEC1322_I2C_CTRL(controller) = CTRL_PIN | CTRL_ESO |
				       CTRL_STO | CTRL_ACK;
	cdata[controller].transaction_state = I2C_TRANSACTION_STOPPED;
	if (ret_done == STS_LRB)
		return EC_ERROR_BUSY;
	else if (ret_done == EC_ERROR_TIMEOUT) {
		/*
		 * If our transaction timed out then our i2c controller
		 * may be wedged without showing any other outward signs
		 * of failure. Reset the controller so that future
		 * transactions have a chance of success.
		 */
		reset_controller(controller);
		return EC_ERROR_TIMEOUT;
	}
	else
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
	int rv;

	i2c_lock(port, 1);
	select_port(port);
	rv = get_line_level(i2c_port_to_controller(port));
	i2c_lock(port, 0);
	return rv;
}

int i2c_port_to_controller(int port)
{
	if (port < 0 || port >= MEC1322_I2C_PORT_COUNT)
		return -1;
	return (port == MEC1322_I2C0_0) ? 0 : port - 1;
}

void i2c_set_timeout(int port, uint32_t timeout)
{
	/* Param is port, but timeout is stored by-controller. */
	cdata[i2c_port_to_controller(port)].timeout_us =
		timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

static void i2c_init(void)
{
	int i;
	int controller;
	int controller0_kbps = -1;

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	for (i = 0; i < i2c_ports_used; ++i) {
		/*
		 * If this controller has multiple ports, check if we already
		 * configured it. If so, ensure previously configured bitrate
		 * matches.
		 */
		controller = i2c_port_to_controller(i2c_ports[i].port);
		if (controller == 0) {
			if (controller0_kbps != -1) {
				ASSERT(controller0_kbps == i2c_ports[i].kbps);
				continue;
			}
			controller0_kbps = i2c_ports[i].kbps;
		}
		configure_controller(controller, i2c_ports[i].kbps);
		cdata[controller].task_waiting = TASK_ID_INVALID;
		cdata[controller].transaction_state = I2C_TRANSACTION_STOPPED;

		/* Use default timeout. */
		i2c_set_timeout(i2c_ports[i].port, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);

static void handle_interrupt(int controller)
{
	int id = cdata[controller].task_waiting;

	/* Clear the interrupt status */
	MEC1322_I2C_COMPLETE(controller) &= (COMP_RW_BITS_MASK | COMP_IDLE);

	/*
	 * Write to control register interferes with I2C transaction.
	 * Instead, let's disable IRQ from the core until the next time
	 * we want to wait for STS_PIN/STS_NBB.
	 */
	task_disable_irq(MEC1322_IRQ_I2C_0 + controller);

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
