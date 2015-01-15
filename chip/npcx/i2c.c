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

/* Data abort timeout unit:ms*/
#define I2C_ABORT_TIMEOUT  10000
/* Maximum time we allow for an I2C transfer */
#define I2C_TIMEOUT_US  (100*MSEC)
/* Marco functions of I2C */
#define I2C_START(port) SET_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_START)
#define I2C_STOP(port)  SET_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_STOP)
#define I2C_NACK(port)  SET_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_ACK)
#define I2C_WRITE_BYTE(port, data) (NPCX_SMBSDA(port) = data)
#define I2C_READ_BYTE(port, data)  (data = NPCX_SMBSDA(port))

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
	SMB_WAIT_REPEAT_START,
};


/* IRQ for each port */
static const uint32_t i2c_irqs[I2C_PORT_COUNT] = {
		NPCX_IRQ_SMB1, NPCX_IRQ_SMB2, NPCX_IRQ_SMB3, NPCX_IRQ_SMB4};
BUILD_ASSERT(ARRAY_SIZE(i2c_irqs) == I2C_PORT_COUNT);

/* I2C port state data */
struct i2c_status {
	int                   flags;     /* Flags (I2C_XFER_*) */
	const uint8_t        *tx_buf;    /* Entry pointer of transmit buffer */
	uint8_t              *rx_buf;    /* Entry pointer of receive buffer  */
	uint16_t              sz_txbuf;  /* Size of Tx buffer in bytes */
	uint16_t              sz_rxbuf;  /* Size of rx buffer in bytes */
	uint16_t              idx_buf;   /* Current index of Tx/Rx buffer */
	uint8_t               slave_addr;/* target slave address */
	enum smb_oper_state_t oper_state;/* smbus operation state */
	enum smb_error        err_code;  /* Error code */
	int                   task_waiting; /* Task waiting on port */
};
/* I2C port state data array */
static struct i2c_status i2c_stsobjs[I2C_PORT_COUNT];

int i2c_bus_busy(int port)
{
	return IS_BIT_SET(NPCX_SMBCST(port), NPCX_SMBCST_BB) ? 1 : 0;
}

void i2c_abort_data(int port)
{
	uint16_t timeout = I2C_ABORT_TIMEOUT;

	/* Generate a STOP condition */
	I2C_STOP(port);

	/* Clear NEGACK, STASTR and BER bits */
	SET_BIT(NPCX_SMBST(port), NPCX_SMBST_BER);
	SET_BIT(NPCX_SMBST(port), NPCX_SMBST_STASTR);
	/*
	 * In Master mode, NEGACK should be cleared only
	 * after generating STOP
	 */
	SET_BIT(NPCX_SMBST(port), NPCX_SMBST_NEGACK);

	/* Wait till STOP condition is generated */
	while (--timeout) {
		msleep(1);
		if (!IS_BIT_SET(NPCX_SMBCTL1(port), NPCX_SMBCTL1_STOP))
			break;
	}

	/* Clear BB (BUS BUSY) bit */
	SET_BIT(NPCX_SMBCST(port), NPCX_SMBCST_BB);
}

void i2c_reset(int port)
{
	uint16_t timeout = I2C_ABORT_TIMEOUT;
	/* Disable the SMB module */
	CLEAR_BIT(NPCX_SMBCTL2(port), NPCX_SMBCTL2_ENABLE);

	while (--timeout) {
		msleep(1);
		/* WAIT FOR SCL & SDA IS HIGH */
		if (IS_BIT_SET(NPCX_SMBCTL3(port), NPCX_SMBCTL3_SCL_LVL) &&
		    IS_BIT_SET(NPCX_SMBCTL3(port), NPCX_SMBCTL3_SDA_LVL))
			break;
	}

	/* Enable the SMB module */
	SET_BIT(NPCX_SMBCTL2(port), NPCX_SMBCTL2_ENABLE);
}

void i2c_recovery(int port)
{
	/* Abort data, generating STOP condition */
	i2c_abort_data(port);

	/* Reset i2c port by re-enable i2c port*/
	i2c_reset(port);
}

enum smb_error i2c_master_transaction(int port)
{
	/* Set i2c mode to object */
	int events = 0;
	volatile struct i2c_status *p_status = i2c_stsobjs + port;

	if (p_status->oper_state == SMB_IDLE) {
		p_status->oper_state = SMB_MASTER_START;
	} else if (p_status->oper_state == SMB_WAIT_REPEAT_START) {
		p_status->oper_state = SMB_REPEAT_START;
		CPUTS("R");
	}

	/* Generate a START condition */
	I2C_START(port);
	CPUTS("ST");

	/* Wait for transfer complete or timeout */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE, I2C_TIMEOUT_US);
	/* Handle timeout */
	if ((events & TASK_EVENT_I2C_IDLE) == 0) {
		/* Recovery I2C port */
		i2c_recovery(port);
		p_status->err_code = SMB_TIMEOUT_ERROR;
	}

	/*
	 * In slave write operation, NACK is OK, otherwise it is a problem
	 */
	else if (p_status->err_code == SMB_BUS_ERROR ||
			p_status->err_code == SMB_MASTER_NO_ADDRESS_MATCH){
		i2c_recovery(port);
	}

	return p_status->err_code;
}

inline void i2c_handle_sda_irq(int port)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + port;
	/* 1 Issue Start is successful ie. write address byte */
	if (p_status->oper_state == SMB_MASTER_START
			|| p_status->oper_state == SMB_REPEAT_START) {
		uint8_t addr = p_status->slave_addr;
		/* Prepare address byte */
		if (p_status->sz_txbuf == 0) {/* Receive mode */
			p_status->oper_state = SMB_READ_OPER;
			/*
			 * Receiving one byte only - set nack just
			 * before writing address byte
			 */
			if (p_status->sz_rxbuf == 1)
				I2C_NACK(port);

			/* Write the address to the bus R bit*/
			I2C_WRITE_BYTE(port, (addr | 0x1));
			CPUTS("-ARR");
		} else {/* Transmit mode */
			p_status->oper_state = SMB_WRITE_OPER;
			/* Write the address to the bus W bit*/
			I2C_WRITE_BYTE(port, addr);
			CPUTS("-ARW");
		}
		/* Completed handling START condition */
		return;
	}
	/* 2 Handle master write operation  */
	else if (p_status->oper_state == SMB_WRITE_OPER) {
		/* all bytes have been written, in a pure write operation */
		if (p_status->idx_buf == p_status->sz_txbuf) {
			/*  no more message */
			if (p_status->sz_rxbuf == 0) {
				/* need to STOP or not */
				if (p_status->flags & I2C_XFER_STOP) {
					/* Issue a STOP condition on the bus */
					I2C_STOP(port);
					CPUTS("-SP");
				}
				/* Clear SDA Status bit by writing dummy byte */
				I2C_WRITE_BYTE(port, 0xFF);
				/* Set error code */
				p_status->err_code = SMB_OK;
				/* Notify upper layer */
				p_status->oper_state
				= (p_status->flags & I2C_XFER_STOP)
					? SMB_IDLE : SMB_WAIT_REPEAT_START;
				task_set_event(p_status->task_waiting,
						TASK_EVENT_I2C_IDLE, 0);
				CPUTS("-END");
			}
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
				I2C_START(port);
				CPUTS("-RST");
				/*
				 * Receiving one byte only - set nack just
				 * before writing address byte
				 */
				if (p_status->sz_rxbuf == 1) {
					I2C_NACK(port);
					CPUTS("-GNA");
				}
				/* Write the address to the bus R bit*/
				I2C_WRITE_BYTE(port, (addr_byte | 0x1));
				CPUTS("-ARR");
			}
		}
		/* write next byte (not last byte and not slave address */
		else {
			I2C_WRITE_BYTE(port,
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
				I2C_STOP(port);
				CPUTS("-SP");
			}
		}
		/* Check if byte-before-last is about to be read */
		else if (p_status->idx_buf == (p_status->sz_rxbuf - 2)) {
			/*
			 * Set nack before reading byte-before-last,
			 * so that nack will be generated after receive
			 * of last byte
			 */
			I2C_NACK(port);
			CPUTS("-GNA");
		}

		/* Read data for SMBSDA */
		I2C_READ_BYTE(port, data);
		CPRINTS("-R(%02x)", data);
		/* Read to buffer */
		p_status->rx_buf[p_status->idx_buf++] = data;

		/* last byte is read - end of transaction */
		if (p_status->idx_buf == p_status->sz_rxbuf) {
			/* Set error code */
			p_status->err_code = SMB_OK;
			/* Notify upper layer of missing data */
			p_status->oper_state = (p_status->flags & I2C_XFER_STOP)
					? SMB_IDLE : SMB_WAIT_REPEAT_START;
			task_set_event(p_status->task_waiting,
					TASK_EVENT_I2C_IDLE, 0);
			CPUTS("-END");
		}
	}
}

void i2c_master_int_handler (int port)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + port;
	/* Condition 1 : A Bus Error has been identified */
	if (IS_BIT_SET(NPCX_SMBST(port), NPCX_SMBST_BER)) {
		/* Clear BER Bit */
		SET_BIT(NPCX_SMBST(port), NPCX_SMBST_BER);
		/* Set error code */
		p_status->err_code = SMB_BUS_ERROR;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE, 0);
		CPUTS("-BER");
	}

	/* Condition 2: A negative acknowledge has occurred */
	if (IS_BIT_SET(NPCX_SMBST(port), NPCX_SMBST_NEGACK)) {
		/* Clear NEGACK Bit */
		SET_BIT(NPCX_SMBST(port), NPCX_SMBST_NEGACK);
		/* Set error code */
		p_status->err_code = SMB_MASTER_NO_ADDRESS_MATCH;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE, 0);
		CPUTS("-NA");
	}

	/* Condition 3: SDA status is set - transmit or receive */
	if (IS_BIT_SET(NPCX_SMBST(port), NPCX_SMBST_SDAST))
		i2c_handle_sda_irq(port);
}

/**
 * Handle an interrupt on the specified port.
 *
 * @param port		I2C port generating interrupt
 */
void handle_interrupt(int port)
{
	i2c_master_int_handler(port);
}

void i2c0_interrupt(void) { handle_interrupt(0); }
void i2c1_interrupt(void) { handle_interrupt(1); }
void i2c2_interrupt(void) { handle_interrupt(2); }
void i2c3_interrupt(void) { handle_interrupt(3); }

DECLARE_IRQ(NPCX_IRQ_SMB1, i2c0_interrupt, 2);
DECLARE_IRQ(NPCX_IRQ_SMB2, i2c1_interrupt, 2);
DECLARE_IRQ(NPCX_IRQ_SMB3, i2c2_interrupt, 2);
DECLARE_IRQ(NPCX_IRQ_SMB4, i2c3_interrupt, 2);

/*****************************************************************************/
/* IC specific low-level driver */

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + port;

	if (port < 0 || port >= i2c_ports_used)
		return EC_ERROR_INVAL;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	/* Copy data to port struct */
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
	if ((flags & I2C_XFER_START) && (i2c_bus_busy(port)
			|| (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);
		/* recovery i2c port */
		i2c_recovery(port);
	}

	/* Enable SMB interrupt and New Address Match interrupt source */
	SET_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_NMINTE);
	SET_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_INTEN);

	CPUTS("\n");

	/* Assign current task ID */
	p_status->task_waiting = task_get_current();

	/* Start master transaction */
	i2c_master_transaction(port);

	/* Reset task ID */
	p_status->task_waiting = TASK_ID_INVALID;

	/* Disable SMB interrupt and New Address Match interrupt source */
	CLEAR_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_NMINTE);
	CLEAR_BIT(NPCX_SMBCTL1(port), NPCX_SMBCTL1_INTEN);

	CPRINTS("-Err:0x%02x\n", p_status->err_code);

	return (p_status->err_code == SMB_OK) ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

/**
 * Return raw I/O line levels (I2C_LINE_*) for a port when port is in alternate
 * function mode.
 *
 * @param port		Port to check
 * @return			State of SCL/SDA bit 0/1
 */
int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		   (i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	/* Check do we support this port of i2c and return gpio number of scl */
	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
#if !(I2C_LEVEL_SUPPORT)
		return gpio_get_level(g);
#else
		return IS_BIT_SET(NPCX_SMBCTL3(port), NPCX_SMBCTL3_SCL_LVL);
#endif

	/* If no SCL pin defined for this port, then return 1 to appear idle */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;
	/* Check do we support this port of i2c and return gpio number of scl */
	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
#if !(I2C_LEVEL_SUPPORT)
		return gpio_get_level(g);
#else
		return IS_BIT_SET(NPCX_SMBCTL3(port), NPCX_SMBCTL3_SDA_LVL);
#endif

	/* If no SDA pin defined for this port, then return 1 to appear idle */
	return 1;
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
		    int len)
{
	int rv;
	uint8_t reg, block_length;

	/* Check block protocol size */
	if ((len <= 0) || (len > 32))
		return EC_ERROR_INVAL;

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
	/* I2C is under APB2 */
	int freq;
	int port;

	for (port = 0; port < i2c_ports_used; port++) {
		int bus_freq = i2c_ports[port].kbps;
		int scl_time;

		/* SMB0/1 use core clock & SMB2/3 use apb2 clock */
		if (port < 2)
			freq = clock_get_freq();
		else
			freq = clock_get_apb2_freq();

		/* use Fast Mode */
		SET_BIT(NPCX_SMBCTL3(port)  , NPCX_SMBCTL3_400K);

		/*
		 * Set SCLLT/SCLHT:
		 * tSCLL = 2 * SCLLT7-0 * tCLK
		 * tSCLH = 2 * SCLHT7-0 * tCLK
		 * (tSCLL+tSCLH) = 4 * SCLH(L)T * tCLK if tSCLL == tSCLH
		 * SCLH(L)T = T(SCL)/4/T(CLK) = FREQ(CLK)/4/FREQ(SCL)
		 */
		scl_time = (freq/1000) / (bus_freq * 4);   /* bus_freq is KHz */

		/* set SCL High/Low time */
		NPCX_SMBSCLLT(port) = scl_time;
		NPCX_SMBSCLHT(port) = scl_time;
	}
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

static void i2c_init(void)
{
	int port = 0;
	/* Configure pins from GPIOs to I2Cs */
	gpio_config_module(MODULE_I2C, 1);

	/* Enable clock for I2C peripheral */
	clock_enable_peripheral(CGC_OFFSET_I2C, CGC_I2C_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);
	/*
	 * initialize smb status and register
	 */
	for (port = 0; port < i2c_ports_used; port++) {
		volatile struct i2c_status *p_status = i2c_stsobjs + port;
		/* Configure pull-up for SMB interface pins */
#ifndef SMB_SUPPORT18V
		/* Enable 3.3V pull-up */
		SET_BIT(NPCX_DEVPU0, port);
#else
		/* Set GPIO Pin voltage judgment to 1.8V */
		SET_BIT(NPCX_LV_GPIO_CTL1, port+1);
#endif

		/* Enable module - before configuring CTL1 */
		SET_BIT(NPCX_SMBCTL2(port), NPCX_SMBCTL2_ENABLE);

		/* status init */
		p_status->oper_state = SMB_IDLE;

		/* Reset task ID */
		p_status->task_waiting = TASK_ID_INVALID;

		/* Enable event and error interrupts */
		task_enable_irq(i2c_irqs[port]);
	}
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);
