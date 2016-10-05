/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for Chrome EC */

#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#if !(DEBUG_I2C)
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#endif

/* Pull-up bit for I2C */
#define NPCX_I2C_PUBIT(controller, port) \
	((controller*2) + port)

/* Timeout for device should be available after reset (SMBus spec. unit:ms) */
#define I2C_MAX_TIMEOUT 35
/*
 * Timeout for SCL held to low by slave device . (SMBus spec. unit:ms).
 * Some I2C devices may violate this timing and clock stretch for longer.
 * TODO: Consider increasing this timeout.
 */
#define I2C_MIN_TIMEOUT 25

/* Marco functions of I2C */
#define I2C_START(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_START)
#define I2C_STOP(ctrl)  SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_STOP)
#define I2C_NACK(ctrl)  SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_ACK)
#define I2C_STALL(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_STASTRE)
#define I2C_WRITE_BYTE(ctrl, data) (NPCX_SMBSDA(ctrl) = data)
#define I2C_READ_BYTE(ctrl, data)  (data = NPCX_SMBSDA(ctrl))

/* Error values that functions can return */
enum smb_error {
	SMB_OK = 0,                 /* No error                            */
	SMB_CH_OCCUPIED,            /* Channel is already occupied         */
	SMB_MEM_POOL_INIT_ERROR,    /* Memory pool initialization error    */
	SMB_BUS_FREQ_ERROR,         /* SMbus freq was not valid            */
	SMB_INVLAID_REGVALUE,       /* Invalid SMbus register value        */
	SMB_UNEXIST_CH_ERROR,       /* Channel does not exist              */
	SMB_NO_SUPPORT_PTL,         /* Not support SMBus Protocol          */
	SMB_BUS_ERROR,              /* Encounter bus error                 */
	SMB_MASTER_NO_ADDRESS_MATCH,/* No slave address match (Master Mode)*/
	SMB_READ_DATA_ERROR,        /* Read data for SDA error             */
	SMB_READ_OVERFLOW_ERROR,    /* Read data over than we predict      */
	SMB_TIMEOUT_ERROR,          /* Timeout expired                     */
	SMB_MODULE_ISBUSY,          /* Module is occupied by other device  */
	SMB_BUS_BUSY,               /* SMBus is occupied by other device   */
};

/*
 * Internal SMBus Interface driver states values, which reflect events
 * which occured on the bus
 */
enum smb_oper_state_t {
	SMB_IDLE,
	SMB_MASTER_START,
	SMB_WRITE_OPER,
	SMB_READ_OPER,
	SMB_REPEAT_START,
	SMB_WRITE_SUSPEND,
	SMB_READ_SUSPEND,
};

/* IRQ for each port */
static const uint32_t i2c_irqs[I2C_CONTROLLER_COUNT] = {
		NPCX_IRQ_SMB1, NPCX_IRQ_SMB2, NPCX_IRQ_SMB3, NPCX_IRQ_SMB4};
BUILD_ASSERT(ARRAY_SIZE(i2c_irqs) == I2C_CONTROLLER_COUNT);

/* I2C controller state data */
struct i2c_status {
	int                   flags;     /* Flags (I2C_XFER_*) */
	const uint8_t        *tx_buf;    /* Entry pointer of transmit buffer */
	uint8_t              *rx_buf;    /* Entry pointer of receive buffer  */
	uint16_t              sz_txbuf;  /* Size of Tx buffer in bytes */
	uint16_t              sz_rxbuf;  /* Size of rx buffer in bytes */
	uint16_t              idx_buf;   /* Current index of Tx/Rx buffer */
	uint8_t               slave_addr;/* Target slave address */
	enum smb_oper_state_t oper_state;/* Smbus operation state */
	enum smb_error        err_code;  /* Error code */
	int                   task_waiting; /* Task waiting on controller */
	uint32_t              timeout_us;/* Transaction timeout */
};
/* I2C controller state data array */
struct i2c_status i2c_stsobjs[I2C_CONTROLLER_COUNT];

/* I2C timing setting */
struct i2c_timing {
	uint8_t clock; /* I2C source clock. (Unit: MHz)*/
	uint8_t HLDT;  /* I2C hold-time. (Unit: clocks) */
	uint8_t k1;    /* k1 = SCL low-time (Unit: clocks) */
	uint8_t k2;    /* k2 = SCL high-time (Unit: clocks) */
};

/* I2C timing setting array of 400K & 1M Hz */
static const struct i2c_timing i2c_400k_timings[] = {
	{50, 22, 84, 56},
	{48, 22, 80, 54},
	{40, 19, 66, 46},
	{33, 17, 52, 38},
	{24, 15, 38, 28},
	{15, 12, 24, 18},
	{13, 11, 22, 16},
	{12, 11, 20, 14},};
const unsigned int i2c_400k_timing_used = ARRAY_SIZE(i2c_400k_timings);

static const struct i2c_timing i2c_1m_timings[] = {
	{50, 13, 32, 24},
	{48, 13, 30, 24},
	{40, 12, 26, 20},
	{33, 11, 20, 16},
	{24, 10, 16, 12},
	{15,  9, 10,  8},};
const unsigned int i2c_1m_timing_used = ARRAY_SIZE(i2c_1m_timings);


int i2c_port_to_controller(int port)
{
	if (port < 0 || port >= I2C_PORT_COUNT)
		return -1;
	return (port == NPCX_I2C_PORT0_0) ? 0 : port - 1;
}

static void i2c_select_port(int port)
{
	/*
	 * I2C0_1 uses port 1 of controller 0. All other I2C pin sets
	 * use port 0.
	 */
	if (port > NPCX_I2C_PORT0_1)
		return;

	/* Select IO pins for multi-ports I2C controllers */
	UPDATE_BIT(NPCX_GLUE_SMBSEL, NPCX_SMBSEL_SMB0SEL,
			(port == NPCX_I2C_PORT0_1));
}

static void i2c_init_bus(int controller)
{
	/* Enable module - before configuring CTL1 */
	SET_BIT(NPCX_SMBCTL2(controller), NPCX_SMBCTL2_ENABLE);

	/* Enable SMB interrupt and New Address Match interrupt source */
	SET_BIT(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_NMINTE);
	SET_BIT(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_INTEN);
}

int i2c_bus_busy(int controller)
{
	return IS_BIT_SET(NPCX_SMBCST(controller), NPCX_SMBCST_BB) ? 1 : 0;
}

static int i2c_wait_stop_completed(int controller, int timeout)
{
	if (timeout <= 0)
		return EC_ERROR_INVAL;

	/* Wait till STOP condition is generated. ie. I2C bus is idle. */
	while (timeout > 0) {
		if (!IS_BIT_SET(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_STOP))
			break;
		if (--timeout > 0)
			msleep(1);
	}

	if (timeout)
		return EC_SUCCESS;
	else
		return EC_ERROR_TIMEOUT;
}

void i2c_abort_data(int controller)
{
	/* Clear NEGACK, STASTR and BER bits */
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_BER);
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_STASTR);
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_NEGACK);

	/* Wait till STOP condition is generated */
	if (i2c_wait_stop_completed(controller, I2C_MAX_TIMEOUT)
			!= EC_SUCCESS) {
		cprints(CC_I2C, "Abort i2c %02x fail!", controller);
		/* Clear BB (BUS BUSY) bit */
		SET_BIT(NPCX_SMBCST(controller), NPCX_SMBCST_BB);
		return;
	}

	/* Clear BB (BUS BUSY) bit */
	SET_BIT(NPCX_SMBCST(controller), NPCX_SMBCST_BB);
}

int i2c_reset(int controller)
{
	uint16_t timeout = I2C_MAX_TIMEOUT;
	/* Disable the SMB module */
	CLEAR_BIT(NPCX_SMBCTL2(controller), NPCX_SMBCTL2_ENABLE);

	while (--timeout) {
		/* WAIT FOR SCL & SDA IS HIGH */
		if (IS_BIT_SET(NPCX_SMBCTL3(controller), NPCX_SMBCTL3_SCL_LVL)
		  && IS_BIT_SET(NPCX_SMBCTL3(controller), NPCX_SMBCTL3_SDA_LVL))
			break;
		msleep(1);
	}

	if (timeout == 0) {
		cprints(CC_I2C, "Reset i2c %02x fail!", controller);
		return 0;
	}

	/* Init the SMB module again */
	i2c_init_bus(controller);
	return 1;
}

void i2c_recovery(int controller, volatile struct i2c_status *p_status)
{
	CPRINTS("i2c %d recovery! error code is %d, current state is %d",
			controller, p_status->err_code, p_status->oper_state);

	/* Abort data, wait for STOP condition completed. */
	i2c_abort_data(controller);

	/* Reset i2c controller by re-enable i2c controller*/
	if (!i2c_reset(controller))
		return;

	/* Restore to idle status */
	p_status->oper_state = SMB_IDLE;
}

enum smb_error i2c_master_transaction(int controller)
{
	/* Set i2c mode to object */
	int events = 0;
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/* Assign current SMB status of controller */
	if (p_status->oper_state == SMB_IDLE) {
		/* New transaction */
		p_status->oper_state = SMB_MASTER_START;
	} else if (p_status->oper_state == SMB_WRITE_SUSPEND) {
		if (p_status->sz_txbuf == 0) {
			/* Read bytes from next transaction */
			p_status->oper_state = SMB_REPEAT_START;
			CPUTS("R");
		} else {
			/* Continue to write the other bytes */
			p_status->oper_state = SMB_WRITE_OPER;
			I2C_WRITE_BYTE(controller,
					p_status->tx_buf[p_status->idx_buf++]);
			CPRINTS("-W(%02x)",
					p_status->tx_buf[p_status->idx_buf-1]);
		}
	} else if (p_status->oper_state == SMB_READ_SUSPEND) {
		/* Need to read the other bytes from next transaction */
		p_status->oper_state = SMB_READ_OPER;
		if (p_status->sz_rxbuf == 1) {
			/*
			 * Since SCL is released after reading last byte from
			 * previous transaction, we have no chance to set NACK
			 * bit if the next transaction is only one byte. Master
			 * cannot generate STOP when the last byte is ACK during
			 * receiving.
			 */
			CPRINTS("I2C %d rxbuf size should exceed one byte in "
					"2th transaction", controller);
			p_status->err_code = SMB_NO_SUPPORT_PTL;
			i2c_recovery(controller, p_status);
			return EC_ERROR_UNKNOWN;
		}
	} else
		cprints(CC_I2C, "Unexpected i2c state machine! %d",
				p_status->oper_state);

	/* Generate a START condition */
	if (p_status->oper_state == SMB_MASTER_START ||
			p_status->oper_state == SMB_REPEAT_START) {
		I2C_START(controller);
		CPUTS("ST");
	}

	/* Enable event and error interrupts */
	task_enable_irq(i2c_irqs[controller]);

	/* Wait for transfer complete or timeout */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE,
			p_status->timeout_us);

	/* Disable event and error interrupts */
	task_disable_irq(i2c_irqs[controller]);

	/*
	 * If Stall-After-Start mode is still enabled since NACK or BUS error
	 * occurs, disable it.
	 */
	if (IS_BIT_SET(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_STASTRE))
		CLEAR_BIT(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_STASTRE);

	/* Handle bus timeout */
	if ((events & TASK_EVENT_I2C_IDLE) == 0) {
		p_status->err_code = SMB_TIMEOUT_ERROR;
		/* Recovery I2C controller */
		i2c_recovery(controller, p_status);
	}
	/* Recovery bus if we encounter bus error */
	else if (p_status->err_code == SMB_BUS_ERROR)
		i2c_recovery(controller, p_status);

	/* Wait till STOP condition is generated for normal transaction */
	if (p_status->err_code == SMB_OK && i2c_wait_stop_completed(controller,
			I2C_MIN_TIMEOUT) != EC_SUCCESS) {
		cprints(CC_I2C, "STOP fail! scl %02x is held by slave device!",
				controller);
		p_status->err_code = SMB_TIMEOUT_ERROR;
	}

	return p_status->err_code;
}

/* Issue stop condition if necessary and end transaction */
void i2c_done(int controller)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/* need to STOP or not */
	if (p_status->flags & I2C_XFER_STOP) {
		/* Issue a STOP condition on the bus */
		I2C_STOP(controller);
		CPUTS("-SP");
		/* Clear SDAST by writing dummy byte */
		I2C_WRITE_BYTE(controller, 0xFF);
	}

	/* Set error code */
	p_status->err_code = SMB_OK;
	/* Set SMB status if we need stall bus */
	p_status->oper_state = (p_status->flags & I2C_XFER_STOP)
				? SMB_IDLE : SMB_WRITE_SUSPEND;
	/*
	 * Disable interrupt for i2c master stall SCL
	 * and forbid SDAST generate interrupt
	 * until common layer start other transactions
	 */
	if (p_status->oper_state == SMB_WRITE_SUSPEND)
		task_disable_irq(i2c_irqs[controller]);

	/* Notify upper layer */
	task_set_event(p_status->task_waiting,
		       TASK_EVENT_I2C_IDLE, 0);
	CPUTS("-END");
}

inline void i2c_handle_sda_irq(int controller)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;
	/* 1 Issue Start is successful ie. write address byte */
	if (p_status->oper_state == SMB_MASTER_START
			|| p_status->oper_state == SMB_REPEAT_START) {
		uint8_t addr = p_status->slave_addr;
		/* Prepare address byte */
		if (p_status->sz_txbuf == 0) {/* Receive mode */
			p_status->oper_state = SMB_READ_OPER;
			/*
			 * Receiving one or zero bytes - stall bus after START
			 * condition. If there's no slave devices on bus, FW
			 * needn't to set ACK bit.
			 */
			if (p_status->sz_rxbuf < 2)
				I2C_STALL(controller);

			/* Write the address to the bus R bit*/
			I2C_WRITE_BYTE(controller, (addr | 0x1));
			CPRINTS("-ARR-0x%02x", addr);
		} else {/* Transmit mode */
			p_status->oper_state = SMB_WRITE_OPER;
			/* Write the address to the bus W bit*/
			I2C_WRITE_BYTE(controller, addr);
			CPRINTS("-ARW-0x%02x", addr);
		}
		/* Completed handling START condition */
		return;
	}
	/* 2 Handle master write operation  */
	else if (p_status->oper_state == SMB_WRITE_OPER) {
		/* all bytes have been written, in a pure write operation */
		if (p_status->idx_buf == p_status->sz_txbuf) {
			/*  no more message */
			if (p_status->sz_rxbuf == 0)
				i2c_done(controller);
			/* need to restart & send slave address immediately */
			else {
				uint8_t addr_byte = p_status->slave_addr;
				/*
				 * Prepare address byte
				 * and start to receive bytes
				 */
				p_status->oper_state = SMB_READ_OPER;
				/* Reset index of buffer */
				p_status->idx_buf = 0;

				/*
				 * Generate (Repeated) Start
				 * upon next write to SDA
				 */
				I2C_START(controller);
				CPUTS("-RST");
				/*
				 * Receiving one byte only - set nack just
				 * before writing address byte
				 */
				if (p_status->sz_rxbuf == 1 &&
					(p_status->flags & I2C_XFER_STOP)) {
					I2C_NACK(controller);
					CPUTS("-GNA");
				}
				/* Write the address to the bus R bit*/
				I2C_WRITE_BYTE(controller, (addr_byte | 0x1));
				CPUTS("-ARR");
			}
		}
		/* write next byte (not last byte and not slave address */
		else {
			I2C_WRITE_BYTE(controller,
					p_status->tx_buf[p_status->idx_buf++]);
			CPRINTS("-W(%02x)",
					p_status->tx_buf[p_status->idx_buf-1]);
		}
	}
	/* 3 Handle master read operation (read or after a write operation) */
	else if (p_status->oper_state == SMB_READ_OPER) {
		uint8_t data;
		/* last byte is about to be read - end of transaction */
		if (p_status->idx_buf == (p_status->sz_rxbuf - 1)) {
			/* need to STOP or not */
			if (p_status->flags & I2C_XFER_STOP) {
				/* Stop should set before reading last byte */
				I2C_STOP(controller);
				CPUTS("-SP");
			} else {
				/*
				 * Disable interrupt before i2c master read SDA
				 * reg (stall SCL) and forbid SDAST generate
				 * interrupt until starting other transactions
				 */
				task_disable_irq(i2c_irqs[controller]);
			}
		}
		/* Check if byte-before-last is about to be read */
		else if (p_status->idx_buf == (p_status->sz_rxbuf - 2)) {
			/*
			 * Set nack before reading byte-before-last,
			 * so that nack will be generated after receive
			 * of last byte
			 */
			if (p_status->flags & I2C_XFER_STOP) {
				I2C_NACK(controller);
				CPUTS("-GNA");
			}
		}

		/* Read data for SMBSDA */
		I2C_READ_BYTE(controller, data);
		CPRINTS("-R(%02x)", data);

		/* Read to buffer */
		p_status->rx_buf[p_status->idx_buf++] = data;

		/* last byte is read - end of transaction */
		if (p_status->idx_buf == p_status->sz_rxbuf) {
			/* Set current status */
			p_status->oper_state = (p_status->flags & I2C_XFER_STOP)
					? SMB_IDLE : SMB_READ_SUSPEND;
			/* Set error code */
			p_status->err_code = SMB_OK;
			/* Notify upper layer of missing data */
			task_set_event(p_status->task_waiting,
					TASK_EVENT_I2C_IDLE, 0);
			CPUTS("-END");
		}
	}
}

void i2c_master_int_handler (int controller)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;
	/* Condition 1 : A Bus Error has been identified */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_BER)) {
		/* Generate a STOP condition */
		I2C_STOP(controller);
		CPUTS("-SP");
		/* Clear BER Bit */
		SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_BER);
		/* Set error code */
		p_status->err_code = SMB_BUS_ERROR;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE, 0);
		CPUTS("-BER");
	}

	/* Condition 2: A negative acknowledge has occurred */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_NEGACK)) {
		/* Generate a STOP condition */
		I2C_STOP(controller);
		CPUTS("-SP");
		/* Clear NEGACK Bit */
		SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_NEGACK);
		/* Set error code */
		p_status->err_code = SMB_MASTER_NO_ADDRESS_MATCH;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE, 0);
		CPUTS("-NA");
	}

	/* Condition 3: A Stall after START has occurred for READ-BYTE */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_STASTR)) {
		CPUTS("-STL");

		/* Disable Stall-After-Start mode first */
		CLEAR_BIT(NPCX_SMBCTL1(controller), NPCX_SMBCTL1_STASTRE);

		/*
		 * Generate stop condition and return success status since
		 * ACK received on zero-byte transaction.
		 */
		if (p_status->sz_rxbuf == 0)
			i2c_done(controller);
		/*
		 * Otherwise we have a one-byte transaction, so nack after
		 * receiving next byte, if requested.
		 */
		else if (p_status->flags & I2C_XFER_STOP)
			I2C_NACK(controller);

		/* Clear STASTR to release SCL after setting NACK/STOP bits */
		SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_STASTR);
	}

	/* Condition 4: SDA status is set - transmit or receive */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_SDAST)) {
		i2c_handle_sda_irq(controller);
#if DEBUG_I2C
		/* SDAST still issued with unexpected state machine */
		if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_SDAST) &&
				p_status->oper_state != SMB_WRITE_SUSPEND) {
			cprints(CC_I2C, "i2c %d unknown state %d, error %d\n",
			  controller, p_status->oper_state, p_status->err_code);
		}
#endif
	}
}

/**
 * Handle an interrupt on the specified controller.
 *
 * @param controller   I2C controller generating interrupt
 */
void handle_interrupt(int controller)
{
	i2c_master_int_handler(controller);
}

void i2c0_interrupt(void) { handle_interrupt(0); }
void i2c1_interrupt(void) { handle_interrupt(1); }
void i2c2_interrupt(void) { handle_interrupt(2); }
void i2c3_interrupt(void) { handle_interrupt(3); }

DECLARE_IRQ(NPCX_IRQ_SMB1, i2c0_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_SMB2, i2c1_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_SMB3, i2c2_interrupt, 3);
DECLARE_IRQ(NPCX_IRQ_SMB4, i2c3_interrupt, 3);

/*****************************************************************************/
/* IC specific low-level driver */

void i2c_set_timeout(int port, uint32_t timeout)
{
	int ctrl = i2c_port_to_controller(port);

	/* Return if i2c_port_to_controller() returned an error */
	if (ctrl < 0)
		return;

	/* Param is port, but timeout is stored by-controller. */
	i2c_stsobjs[ctrl].timeout_us =
		timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	volatile struct i2c_status *p_status;
	int ctrl = i2c_port_to_controller(port);

	/* Return error if i2c_port_to_controller() returned an error */
	if (ctrl < 0)
		return EC_ERROR_INVAL;

	p_status = i2c_stsobjs + ctrl;

	/* Assign current task ID */
	p_status->task_waiting = task_get_current();

	/* Select port for multi-ports i2c controller */
	i2c_select_port(port);

	/* Copy data to controller struct */
	p_status->flags       = flags;
	p_status->tx_buf      = out;
	p_status->sz_txbuf    = out_size;
	p_status->rx_buf      = in;
	p_status->sz_rxbuf    = in_size;
#if I2C_7BITS_ADDR
	/* Set slave address from 7-bits to 8-bits */
	p_status->slave_addr  = (slave_addr<<1);
#else
	/* Set slave address (8-bits) */
	p_status->slave_addr  = slave_addr;
#endif
	/* Reset index & error */
	p_status->idx_buf     = 0;
	p_status->err_code    = SMB_OK;

	/* Make sure we're in a good state to start */
	if ((flags & I2C_XFER_START) &&
	     /* Ignore busy bus for repeated start */
	     p_status->oper_state != SMB_WRITE_SUSPEND &&
	     (i2c_bus_busy(ctrl)
	     || (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {

		/* Attempt to unwedge the i2c port. */
		i2c_unwedge(port);
		p_status->err_code = SMB_BUS_BUSY;
		/* recovery i2c controller */
		i2c_recovery(ctrl, p_status);
		/* Select port again for recovery */
		i2c_select_port(port);
	}

	CPUTS("\n");

	/* Start master transaction */
	i2c_master_transaction(ctrl);

	/* Reset task ID */
	p_status->task_waiting = TASK_ID_INVALID;

	CPRINTS("-Err:0x%02x\n", p_status->err_code);

	return (p_status->err_code == SMB_OK) ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

/**
 * Return raw I/O line levels (I2C_LINE_*) for a port when port is in alternate
 * function mode.
 *
 * @param port  Port to check
 * @return      State of SCL/SDA bit 0/1
 */
int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		   (i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

/*
 * Due to we couldn't support GPIO reading when IO is selected SMBus, we need
 * to distingulish which mode we used currently.
 */
int i2c_is_raw_mode(int port)
{
	int bit = (port > NPCX_I2C_PORT0_1) ? ((port - 1) * 2) : port;

	if (IS_BIT_SET(NPCX_DEVALT(2), bit))
		return 0;
	else
		return 1;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	/*
	 * Check do we support this port of i2c and return gpio number of scl.
	 * Please notice we cannot read voltage level from GPIO in M4 EC
	 */
	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS) {
		if (i2c_is_raw_mode(port))
			return gpio_get_level(g);
		else
			return IS_BIT_SET(NPCX_SMBCTL3(
			i2c_port_to_controller(port)), NPCX_SMBCTL3_SCL_LVL);
	}

	/* If no SCL pin defined for this port, then return 1 to appear idle */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	/*
	 * Check do we support this port of i2c and return gpio number of scl.
	 * Please notice we cannot read voltage level from GPIO in M4 EC
	 */
	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS) {
		if (i2c_is_raw_mode(port))
			return gpio_get_level(g);
		else
			return IS_BIT_SET(NPCX_SMBCTL3(
			i2c_port_to_controller(port)), NPCX_SMBCTL3_SDA_LVL);
	}


	/* If no SDA pin defined for this port, then return 1 to appear idle */
	return 1;
}

/*****************************************************************************/
/* Hooks */
static void i2c_freq_changed(void)
{
	int freq, i, j;

	for (i = 0; i < i2c_ports_used; i++) {
		int bus_freq = i2c_ports[i].kbps;
		int ctrl = i2c_port_to_controller(i2c_ports[i].port);
		int scl_freq;

		/* SMB0/1 use core clock & SMB2/3 use apb2 clock */
		if (ctrl < 2)
			freq = clock_get_freq();
		else
			freq = clock_get_apb2_freq();

		/*
		 * Set SCL frequency by formula:
		 * tSCL = 4 * SCLFRQ * tCLK
		 * fSCL = fCLK / (4*SCLFRQ)
		 * SCLFRQ = fSCL/(4*fSCL)
		 */
		scl_freq = (freq/1000) / (bus_freq*4); /* bus_freq is KHz */

		/* Normal mode if i2c freq is under 100kHz */
		if (bus_freq <= 100) {
			/* Set divider value of SCL */
			SET_FIELD(NPCX_SMBCTL2(ctrl), NPCX_SMBCTL2_SCLFRQ7_FIELD
					, (scl_freq & 0x7F));
			SET_FIELD(NPCX_SMBCTL3(ctrl), NPCX_SMBCTL3_SCLFRQ2_FIELD
					, (scl_freq >> 7));
		} else {
			const struct i2c_timing *pTiming;
			int i2c_timing_used;

			/* use Fast Mode */
			SET_BIT(NPCX_SMBCTL3(ctrl)  , NPCX_SMBCTL3_400K);
			/*
			 * Set SCLH(L)T and hold-time directly for best i2c
			 * timing condition for all source clocks. Please refer
			 * Section 7.5.9 "SMBus Timing - Fast Mode" for detail.
			 */
			if (bus_freq == 400) {
				pTiming = i2c_400k_timings;
				i2c_timing_used = i2c_400k_timing_used;
			} else if (bus_freq == 1000) {
				pTiming = i2c_1m_timings;
				i2c_timing_used = i2c_1m_timing_used;
			} else {
				/* Set value from formula */
				NPCX_SMBSCLLT(ctrl) = scl_freq;
				NPCX_SMBSCLHT(ctrl) = scl_freq;
				cprints(CC_I2C, "Warning: Use 400K or 1MHz",
					"for better timing of I2c %d", ctrl);
				continue;
			}

			for (j = 0; j < i2c_timing_used; j++, pTiming++) {
				if (pTiming->clock == (freq/SECOND)) {
					/* Set SCLH(L)T and hold-time */
					NPCX_SMBSCLLT(ctrl) = pTiming->k1/2;
					NPCX_SMBSCLHT(ctrl) = pTiming->k2/2;
					SET_FIELD(NPCX_SMBCTL4(ctrl),
					NPCX_SMBCTL4_HLDT_FIELD, pTiming->HLDT);
					break;
				}
			}
			if (j == i2c_timing_used)
				cprints(CC_I2C, "Error: Please make sure src ",
					"clock of i2c %d is supported", ctrl);
		}
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

static void i2c_init(void)
{
	int i;
	/* Configure pins from GPIOs to I2Cs */
	gpio_config_module(MODULE_I2C, 1);

	/* Enable clock for I2C peripheral */
	clock_enable_peripheral(CGC_OFFSET_I2C, CGC_I2C_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Set I2C freq */
	i2c_freq_changed();
	/*
	 * initialize smb status and register
	 */
	for (i = 0; i < i2c_ports_used; i++) {
		volatile struct i2c_status *p_status;
		int port = i2c_ports[i].port;
		int ctrl = i2c_port_to_controller(port);

		/* ignore the port if i2c_port_to_controller() failed */
		if (ctrl < 0)
			continue;

		p_status = i2c_stsobjs + ctrl;

		/* status init */
		p_status->oper_state = SMB_IDLE;

		/* Reset task ID */
		p_status->task_waiting = TASK_ID_INVALID;

		/* Use default timeout. */
		i2c_set_timeout(port, 0);

		/* Init the SMB module */
		i2c_init_bus(ctrl);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);

