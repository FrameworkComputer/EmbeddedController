/* Copyright 2014 The ChromiumOS Authors
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
#include "i2c_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#if !(DEBUG_I2C)
#define CPUTS(...)
#define CPRINTS(...)
#define CPRINTF(...)
#else
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)
#endif

/* Timeout for device should be available after reset (SMBus spec. unit:ms) */
#define I2C_MAX_TIMEOUT 35
/*
 * Timeout for SCL held to low by peripheral device. (SMBus spec. unit:ms).
 * Some I2C devices may violate this timing and clock stretch for longer.
 * TODO: Consider increasing this timeout.
 */
#define I2C_MIN_TIMEOUT 25

/*
 * I2C module that supports FIFO mode has 32 bytes Tx FIFO and
 * 32 bytes Rx FIFO.
 */
#define NPCX_I2C_FIFO_MAX_SIZE 32

/* Macro functions of I2C */
#define I2C_START(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_START)
#define I2C_STOP(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_STOP)
#define I2C_NACK(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_ACK)
/* I2C module automatically stall bus after sending peripheral address */
#define I2C_STALL(ctrl) SET_BIT(NPCX_SMBCTL1(ctrl), NPCX_SMBCTL1_STASTRE)
#define I2C_WRITE_BYTE(ctrl, data) (NPCX_SMBSDA(ctrl) = data)
#define I2C_READ_BYTE(ctrl, data) (data = NPCX_SMBSDA(ctrl))
#define I2C_TX_FIFO_OCCUPIED(ctrl) (NPCX_SMBTXF_STS(ctrl) & 0x3F)
#define I2C_TX_FIFO_AVAILABLE(ctrl) \
	(NPCX_I2C_FIFO_MAX_SIZE - I2C_TX_FIFO_OCCUPIED(ctrl))

#define I2C_RX_FIFO_OCCUPIED(ctrl) (NPCX_SMBRXF_STS(ctrl) & 0x3F)
#define I2C_RX_FIFO_AVAILABLE(ctrl) \
	(NPCX_I2C_FIFO_MAX_SIZE - I2C_RX_FIFO_OCCUPIED(ctrl))
/* Drive the SCL signal to low */
#define I2C_SCL_STALL(ctrl)                                          \
	(NPCX_SMBCTL3(ctrl) =                                        \
		 (NPCX_SMBCTL3(ctrl) & ~BIT(NPCX_SMBCTL3_SCL_LVL)) | \
		 BIT(NPCX_SMBCTL3_SDA_LVL))
/*
 * Release the SCL signal to be pulled up to high level.
 * Note: The SCL might be still driven low either by I2C module or external
 * devices connected to ths bus.
 */
#define I2C_SCL_FREE(ctrl)                                 \
	(NPCX_SMBCTL3(ctrl) |= BIT(NPCX_SMBCTL3_SCL_LVL) | \
			       BIT(NPCX_SMBCTL3_SDA_LVL))

/* Error values that functions can return */
enum smb_error {
	SMB_OK = 0, /* No error                           */
	SMB_CH_OCCUPIED, /* Channel is already occupied        */
	SMB_MEM_POOL_INIT_ERROR, /* Memory pool initialization error   */
	SMB_BUS_FREQ_ERROR, /* SMbus freq was not valid           */
	SMB_INVLAID_REGVALUE, /* Invalid SMbus register value       */
	SMB_UNEXIST_CH_ERROR, /* Channel does not exist             */
	SMB_NO_SUPPORT_PTL, /* Not support SMBus Protocol         */
	SMB_BUS_ERROR, /* Encounter bus error                */
	SMB_NO_ADDRESS_MATCH, /* No peripheral address match        */
	/*  (Controller Mode)                 */
	SMB_READ_DATA_ERROR, /* Read data for SDA error            */
	SMB_READ_OVERFLOW_ERROR, /* Read data over than we predict     */
	SMB_TIMEOUT_ERROR, /* Timeout expired                    */
	SMB_MODULE_ISBUSY, /* Module is occupied by other device */
	SMB_BUS_BUSY, /* SMBus is occupied by other device  */
};

/*
 * Internal SMBus Interface driver states values, which reflect events
 * which occurred on the bus
 */
enum smb_oper_state_t {
	SMB_IDLE,
	SMB_CONTROLLER_START,
	SMB_WRITE_OPER,
	SMB_READ_OPER,
	SMB_FAKE_READ_OPER,
	SMB_REPEAT_START,
	SMB_WRITE_SUSPEND,
	SMB_READ_SUSPEND,
};

/* I2C controller state data */
struct i2c_status {
	int flags; /* Flags (I2C_XFER_*) */
	const uint8_t *tx_buf; /* Entry pointer of transmit buffer */
	uint8_t *rx_buf; /* Entry pointer of receive buffer  */
	uint16_t sz_txbuf; /* Size of Tx buffer in bytes */
	uint16_t sz_rxbuf; /* Size of rx buffer in bytes */
	uint16_t idx_buf; /* Current index of Tx/Rx buffer */
	uint16_t addr_flags; /* Target address */
	enum smb_oper_state_t oper_state; /* Smbus operation state */
	enum smb_error err_code; /* Error code */
	int task_waiting; /* Task waiting on controller */
	uint32_t timeout_us; /* Transaction timeout */
	uint16_t kbps; /* Speed */
};
/* I2C controller state data array */
static struct i2c_status i2c_stsobjs[I2C_CONTROLLER_COUNT];

/* I2C timing setting */
struct i2c_timing {
	uint8_t clock; /* I2C source clock. (Unit: MHz)*/
	uint8_t HLDT; /* I2C hold-time. (Unit: clocks) */
	uint8_t k1; /* k1 = SCL low-time (Unit: clocks) */
	uint8_t k2; /* k2 = SCL high-time (Unit: clocks) */
};

/* I2C timing setting array of 400K & 1M Hz */
static const struct i2c_timing i2c_400k_timings[] = {
	{ 20, 7, 32, 22 },
	{ 15, 7, 24, 18 },
};
const unsigned int i2c_400k_timing_used = ARRAY_SIZE(i2c_400k_timings);

static const struct i2c_timing i2c_1m_timings[] = {
	{ 20, 7, 16, 10 },
	{ 15, 7, 14, 10 },
};
const unsigned int i2c_1m_timing_used = ARRAY_SIZE(i2c_1m_timings);

/* IRQ for each port */
const uint32_t i2c_irqs[I2C_CONTROLLER_COUNT] = {
	NPCX_IRQ_SMB1, NPCX_IRQ_SMB2, NPCX_IRQ_SMB3, NPCX_IRQ_SMB4,
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	NPCX_IRQ_SMB5, NPCX_IRQ_SMB6, NPCX_IRQ_SMB7, NPCX_IRQ_SMB8,
#endif
};
BUILD_ASSERT(ARRAY_SIZE(i2c_irqs) == I2C_CONTROLLER_COUNT);

static void i2c_init_bus(int controller)
{
	/* Enable FIFO mode */
	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
		SET_BIT(NPCX_SMBFIF_CTL(controller), NPCX_SMBFIF_CTL_FIFO_EN);

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
			crec_msleep(1);
	}

	if (timeout)
		return EC_SUCCESS;
	else
		return EC_ERROR_TIMEOUT;
}

static void i2c_abort_data(int controller)
{
	/* Clear NEGACK, STASTR and BER bits */
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_BER);
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_STASTR);
	SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_NEGACK);

	/* Wait till STOP condition is generated */
	if (i2c_wait_stop_completed(controller, I2C_MAX_TIMEOUT) !=
	    EC_SUCCESS) {
		cprintf(CC_I2C, "Abort i2c %02x fail!\n", controller);
		/* Clear BB (BUS BUSY) bit */
		SET_BIT(NPCX_SMBCST(controller), NPCX_SMBCST_BB);
		return;
	}

	/* Clear BB (BUS BUSY) bit */
	SET_BIT(NPCX_SMBCST(controller), NPCX_SMBCST_BB);
}

static int i2c_reset(int controller)
{
	uint16_t timeout = I2C_MAX_TIMEOUT;

	/* Disable the SMB module */
	CLEAR_BIT(NPCX_SMBCTL2(controller), NPCX_SMBCTL2_ENABLE);

	while (--timeout) {
		/* WAIT FOR SCL & SDA IS HIGH */
		if (IS_BIT_SET(NPCX_SMBCTL3(controller),
			       NPCX_SMBCTL3_SCL_LVL) &&
		    IS_BIT_SET(NPCX_SMBCTL3(controller), NPCX_SMBCTL3_SDA_LVL))
			break;
		crec_msleep(1);
	}

	if (timeout == 0) {
		cprintf(CC_I2C, "Reset i2c %02x fail!\n", controller);
		return 0;
	}

	/* Init the SMB module again */
	i2c_init_bus(controller);
	return 1;
}

static void i2c_select_bank(int controller, int bank)
{
	if (bank)
		SET_BIT(NPCX_SMBCTL3(controller), NPCX_SMBCTL3_BNK_SEL);
	else
		CLEAR_BIT(NPCX_SMBCTL3(controller), NPCX_SMBCTL3_BNK_SEL);
}

static void i2c_stall_bus(int controller, int stall)
{
	i2c_select_bank(controller, 0);
	/*
	 * Enable the writing to SCL_LVL and SDA_LVL bit in
	 * SMBnCTL3 register. Then, firmware can set SCL_LVL to 0 to
	 * stall the bus when needed. Note: this register should be
	 * accessed when bank = 0.
	 */
	SET_BIT(NPCX_SMBCTL4(controller), NPCX_SMBCTL4_LVL_WE);
	if (stall)
		I2C_SCL_STALL(controller);
	else
		I2C_SCL_FREE(controller);
	/*
	 * Disable the writing to SCL_LVL and SDA_LVL bit in
	 * SMBnCTL3 register. It will prevent form changing the level of
	 * SCL/SDA when touching other bits in SMBnCTL3 register.
	 */
	CLEAR_BIT(NPCX_SMBCTL4(controller), NPCX_SMBCTL4_LVL_WE);
	i2c_select_bank(controller, 1);
}

static void i2c_recovery(int controller, volatile struct i2c_status *p_status)
{
	cprintf(CC_I2C,
		"i2c %d recovery! error code is %d, current state is %d\n",
		controller, p_status->err_code, p_status->oper_state);

	/* Make sure the bus is not stalled before exit. */
	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
		i2c_stall_bus(controller, 0);

	/* Abort data, wait for STOP condition completed. */
	i2c_abort_data(controller);

	/* Reset i2c controller by re-enable i2c controller*/
	if (!i2c_reset(controller))
		return;

	/* Restore to idle status */
	p_status->oper_state = SMB_IDLE;
}

/*
 * This function can be called in either single-byte mode or FIFO mode.
 * In single-byte mode - it always write 1 byte to SMBSDA register at one time.
 * In FIFO mode - write as many as available bytes in FIFO at one time.
 */
static void i2c_fifo_write_data(int controller)
{
	int len, fifo_avail, i;

	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	len = 1;
	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
		len = p_status->sz_txbuf - p_status->idx_buf;
		fifo_avail = I2C_TX_FIFO_AVAILABLE(controller);
		len = MIN(len, fifo_avail);
	}
	for (i = 0; i < len; i++) {
		I2C_WRITE_BYTE(controller,
			       p_status->tx_buf[p_status->idx_buf++]);
		CPRINTF("%02x ", p_status->tx_buf[p_status->idx_buf - 1]);
	}
	CPRINTF("\n");
}

enum smb_error i2c_controller_transaction(int controller)
{
	/* Set i2c mode to object */
	int events = 0;
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/* Switch to bank 1 to access I2C FIO registers */
	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
		i2c_select_bank(controller, 1);

	/* Assign current SMB status of controller */
	if (p_status->oper_state == SMB_IDLE) {
		/* New transaction */
		p_status->oper_state = SMB_CONTROLLER_START;
		/* Clear FIFO and status bit */
		if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
			NPCX_SMBFIF_CTS(controller) =
				BIT(NPCX_SMBFIF_CTS_RXF_TXE) |
				BIT(NPCX_SMBFIF_CTS_CLR_FIFO);
		}
	} else if (p_status->oper_state == SMB_WRITE_SUSPEND) {
		if (p_status->sz_txbuf == 0) {
			/* Read bytes from next transaction */
			p_status->oper_state = SMB_REPEAT_START;
			CPUTS("R");
		} else {
			/* Continue to write the other bytes */
			p_status->oper_state = SMB_WRITE_OPER;
			CPRINTS("-W");
			/*
			 * This function can be called in either single-byte
			 * mode or FIFO mode.
			 */
			i2c_fifo_write_data(controller);
		}
	} else if (p_status->oper_state == SMB_READ_SUSPEND) {
		if (!IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
			/*
			 * Do extra read if read length is 1 and I2C_XFER_STOP
			 * is set simultaneously.
			 */
			if (p_status->sz_rxbuf == 1 &&
			    (p_status->flags & I2C_XFER_STOP)) {
				/*
				 * Since SCL is released after reading last
				 * byte from previous transaction, adding a
				 * extra byte for next transaction which let
				 * ec sets NACK bit in time is necessary.
				 * Or i2c controller cannot generate STOP
				 * when the last byte is ACK during receiving.
				 */
				p_status->sz_rxbuf++;
				p_status->oper_state = SMB_FAKE_READ_OPER;
			} else
				/*
				 * Need to read the other bytes from
				 * next transaction
				 */
				p_status->oper_state = SMB_READ_OPER;
		}
	} else
		cprintf(CC_I2C, "Unexpected i2c state machine! %d\n",
			p_status->oper_state);

	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
		if (p_status->sz_rxbuf > 0) {
			if (p_status->sz_rxbuf > NPCX_I2C_FIFO_MAX_SIZE) {
				/* Set RX threshold = FIFO_MAX_SIZE */
				SET_FIELD(NPCX_SMBRXF_CTL(controller),
					  NPCX_SMBRXF_CTL_RX_THR,
					  NPCX_I2C_FIFO_MAX_SIZE);
			} else {
				/*
				 * set RX threshold = remaining data bytes
				 * (it should be <= FIFO_MAX_SIZE)
				 */
				SET_FIELD(NPCX_SMBRXF_CTL(controller),
					  NPCX_SMBRXF_CTL_RX_THR,
					  p_status->sz_rxbuf);
				/*
				 * Set LAST bit generate the NACK at the
				 * last byte of the data group in FIFO
				 */
				if (p_status->flags & I2C_XFER_STOP) {
					SET_BIT(NPCX_SMBRXF_CTL(controller),
						NPCX_SMBRXF_CTL_LAST);
				}
			}

			/* Free the stalled SCL signal */
			if (p_status->oper_state == SMB_READ_SUSPEND) {
				p_status->oper_state = SMB_READ_OPER;
				i2c_stall_bus(controller, 0);
			}
		}
	}

	/* Generate a START condition */
	if (p_status->oper_state == SMB_CONTROLLER_START ||
	    p_status->oper_state == SMB_REPEAT_START) {
		I2C_START(controller);
		CPUTS("ST");
	}

	/* Enable event and error interrupts */
	task_enable_irq(i2c_irqs[controller]);

	/* Wait for transfer complete or timeout */
	events =
		task_wait_event_mask(TASK_EVENT_I2C_IDLE, p_status->timeout_us);

	/* Disable event and error interrupts */
	task_disable_irq(i2c_irqs[controller]);

	/*
	 * Accessing FIFO register is only needed during transaction.
	 * Switch back to bank 0 at the end of transaction
	 */
	if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
		i2c_select_bank(controller, 0);

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
	if (p_status->err_code == SMB_OK &&
	    i2c_wait_stop_completed(controller, I2C_MIN_TIMEOUT) !=
		    EC_SUCCESS) {
		cprintf(CC_I2C,
			"STOP fail! scl %02x is held by slave device!\n",
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
		/* Clear RXF_TXE bit (RX FIFO full/TX FIFO empty) */
		if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
			NPCX_SMBFIF_CTS(controller) =
				BIT(NPCX_SMBFIF_CTS_RXF_TXE);

		/* Clear SDAST by writing mock byte */
		I2C_WRITE_BYTE(controller, 0xFF);
	}

	/* Set error code */
	p_status->err_code = SMB_OK;
	/* Set SMB status if we need stall bus */
	p_status->oper_state = (p_status->flags & I2C_XFER_STOP) ?
				       SMB_IDLE :
				       SMB_WRITE_SUSPEND;
	/*
	 * Disable interrupt for i2c controller stall SCL
	 * and forbid SDAST generate interrupt
	 * until common layer start other transactions
	 */
	if (p_status->oper_state == SMB_WRITE_SUSPEND)
		task_disable_irq(i2c_irqs[controller]);

	/* Notify upper layer */
	task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE);
	CPUTS("-END");
}

static void i2c_handle_receive(int controller)
{
	uint8_t data;
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/* last byte is about to be read - end of transaction */
	if (p_status->idx_buf == (p_status->sz_rxbuf - 1)) {
		/* need to STOP or not */
		if (p_status->flags & I2C_XFER_STOP) {
			/* Stop should set before reading last byte */
			I2C_STOP(controller);
			CPUTS("-SP");
		} else {
			/*
			 * Disable interrupt before i2c controller read SDA
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

	/* Read to buf. Skip last byte if meet SMB_FAKE_READ_OPER */
	if (p_status->oper_state == SMB_FAKE_READ_OPER &&
	    p_status->idx_buf == (p_status->sz_rxbuf - 1))
		p_status->idx_buf++;
	else
		p_status->rx_buf[p_status->idx_buf++] = data;

	/* last byte is read - end of transaction */
	if (p_status->idx_buf == p_status->sz_rxbuf) {
		/* Set current status */
		p_status->oper_state = (p_status->flags & I2C_XFER_STOP) ?
					       SMB_IDLE :
					       SMB_READ_SUSPEND;
		/* Set error code */
		p_status->err_code = SMB_OK;
		/* Notify upper layer of missing data */
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE);
		CPUTS("-END");
	}
}

static void i2c_fifo_read_data(int controller, uint8_t bytes_in_fifo)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	while (bytes_in_fifo--) {
		uint8_t data;

		data = NPCX_SMBSDA(controller);
		p_status->rx_buf[p_status->idx_buf++] = data;
		CPRINTF("%02x ", data);
	}
	CPRINTF("\n");
}

static void i2c_fifo_handle_receive(int controller)
{
	uint8_t bytes_in_fifo, remaining_bytes;

	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/*
	 * Clear RX_THST bit (RX-FIFO Threshold Status).
	 * It is set when RX_BYTES = RX_THR after being RX_BYTES < RX_THR
	 */
	SET_BIT(NPCX_SMBRXF_STS(controller), NPCX_SMBRXF_STS_RX_THST);
	SET_BIT(NPCX_SMBFIF_CTS(controller), NPCX_SMBFIF_CTS_RXF_TXE);

	bytes_in_fifo = I2C_RX_FIFO_OCCUPIED(controller);
	remaining_bytes = p_status->sz_rxbuf - p_status->idx_buf;
	if (remaining_bytes - bytes_in_fifo <= 0) {
		/*
		 * Last byte is about to be read - end of transaction.
		 * Stop should be set before reading last byte.
		 */
		if (p_status->flags & I2C_XFER_STOP) {
			I2C_STOP(controller);
			CPUTS("-FSP");
		} else {
			task_disable_irq(i2c_irqs[controller]);
			/*
			 * The I2C bus will be freed from stalled and continue
			 * to recevie data when reading data from FIFO.
			 * Pull SCL signal down to stall the bus manually.
			 * SCL signal will be freed when it gets a new I2C
			 * transaction call from common layer.
			 */
			i2c_stall_bus(controller, 1);
		}

		CPRINTS("-LFR");
		i2c_fifo_read_data(controller, remaining_bytes);
	} else {
		CPRINTS("-FR");
		/*
		 * The I2C bus will be freed from stalled and continue to
		 * recevie data when reading data from FIFO.
		 * This may caue driver cannot set the new Rx threshold in time.
		 * Manually stall SCL signal until the new Rx threshold is set.
		 */
		i2c_stall_bus(controller, 1);
		i2c_fifo_read_data(controller, bytes_in_fifo);
		remaining_bytes = p_status->sz_rxbuf - p_status->idx_buf;
		if (remaining_bytes > 0) {
			if (remaining_bytes > NPCX_I2C_FIFO_MAX_SIZE) {
				SET_FIELD(NPCX_SMBRXF_CTL(controller),
					  NPCX_SMBRXF_CTL_RX_THR,
					  NPCX_I2C_FIFO_MAX_SIZE);
			} else {
				SET_FIELD(NPCX_SMBRXF_CTL(controller),
					  NPCX_SMBRXF_CTL_RX_THR,
					  remaining_bytes);
				if (p_status->flags & I2C_XFER_STOP) {
					SET_BIT(NPCX_SMBRXF_CTL(controller),
						NPCX_SMBRXF_CTL_LAST);
					CPRINTS("-FGNA");
				}
			}
		}
		i2c_stall_bus(controller, 0);
	}
	/* last byte is read - end of transaction */
	if (p_status->idx_buf == p_status->sz_rxbuf) {
		/* Set current status */
		p_status->oper_state = (p_status->flags & I2C_XFER_STOP) ?
					       SMB_IDLE :
					       SMB_READ_SUSPEND;
		/* Set error code */
		p_status->err_code = SMB_OK;
		/* Notify upper layer of missing data */
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE);
		CPUTS("-END");
	}
}

static void i2c_handle_sda_irq(int controller)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;
	uint8_t addr_8bit = I2C_STRIP_FLAGS(p_status->addr_flags) << 1;

	/* 1 Issue Start is successful ie. write address byte */
	if (p_status->oper_state == SMB_CONTROLLER_START ||
	    p_status->oper_state == SMB_REPEAT_START) {
		/* Prepare address byte */
		if (p_status->sz_txbuf == 0) { /* Receive mode */
			p_status->oper_state = SMB_READ_OPER;
			/*
			 * Receiving one or zero bytes - stall bus after
			 * START condition. If there's no peripheral
			 * devices on bus, FW needn't to set ACK bit.
			 */
			if (p_status->sz_rxbuf < 2)
				I2C_STALL(controller);

			/* Write the address to the bus R bit*/
			I2C_WRITE_BYTE(controller, (addr_8bit | 0x1));
			CPRINTS("-ARR-0x%02x", addr_8bit);
		} else { /* Transmit mode */
			p_status->oper_state = SMB_WRITE_OPER;
			/* Write the address to the bus W bit*/
			I2C_WRITE_BYTE(controller, addr_8bit);
			CPRINTS("-ARW-0x%02x", addr_8bit);
		}
		/* Completed handling START condition */
		return;
	}
	/* 2 Handle controller write operation */
	else if (p_status->oper_state == SMB_WRITE_OPER) {
		/* all bytes have been written, in a pure write operation */
		if (p_status->idx_buf == p_status->sz_txbuf) {
			/*  no more message */
			if (p_status->sz_rxbuf == 0)
				i2c_done(controller);
			/*
			 * need to restart & send peripheral address
			 * immediately
			 */
			else {
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
				 * Receiving one byte only - set NACK just
				 * before writing address byte.
				 * Set NACK (ACK bit in the SMBnCTL1 register)
				 * only in the single-byte mode.
				 * In FIFO mode, NACK is set via LAST bit
				 * in the SMBnTXF_CTL register.
				 */
				if (p_status->sz_rxbuf == 1 &&
				    (p_status->flags & I2C_XFER_STOP) &&
				    !IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
					I2C_NACK(controller);
					CPUTS("-GNA");
				}
				/* Write the address to the bus R bit*/
				I2C_WRITE_BYTE(controller, (addr_8bit | 0x1));
				CPUTS("-ARR");
			}
		}
		/*
		 * write next byte (not last byte and not peripheral
		 * address)
		 */
		else {
			/*
			 * This function can be called in either single-byte
			 * mode or FIFO mode.
			 */
			CPRINTS("-W");
			i2c_fifo_write_data(controller);
		}
	}
	/*
	 * 3 Handle controller read operation (read or after a write
	 * operation)
	 */
	else if (p_status->oper_state == SMB_READ_OPER ||
		 p_status->oper_state == SMB_FAKE_READ_OPER) {
		if (IS_ENABLED(NPCX_I2C_FIFO_SUPPORT))
			i2c_fifo_handle_receive(controller);
		else
			i2c_handle_receive(controller);
	}
}

static void i2c_controller_int_handler(int controller)
{
	volatile struct i2c_status *p_status = i2c_stsobjs + controller;

	/* Condition 1 : A Bus Error has been identified */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_BER)) {
		uint8_t __attribute__((unused)) data;
		/* Generate a STOP condition */
		I2C_STOP(controller);
		CPUTS("-SP");
		/* Clear BER Bit */
		SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_BER);
		/* Make sure peripheral doesn't hold bus by reading */
		I2C_READ_BYTE(controller, data);

		/* Set error code */
		p_status->err_code = SMB_BUS_ERROR;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE);
		CPUTS("-BER");

		/*
		 * Disable smb's interrupts to forbid ec to enter ISR again
		 * before executing error recovery.
		 */
		task_disable_irq(i2c_irqs[controller]);

		/* return for executing error recovery immediately */
		return;
	}

	/* Condition 2: A negative acknowledge has occurred */
	if (IS_BIT_SET(NPCX_SMBST(controller), NPCX_SMBST_NEGACK)) {
		/* Generate a STOP condition */
		I2C_STOP(controller);
		CPUTS("-SP");
		/* Clear NEGACK Bit */
		SET_BIT(NPCX_SMBST(controller), NPCX_SMBST_NEGACK);
		/* Set error code */
		p_status->err_code = SMB_NO_ADDRESS_MATCH;
		/* Notify upper layer */
		p_status->oper_state = SMB_IDLE;
		task_set_event(p_status->task_waiting, TASK_EVENT_I2C_IDLE);
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
		 * Otherwise we have a one-byte transaction, so NACK after
		 * receiving next byte, if requested.
		 * Set NACK (ACK bit in the SMBnCTL1 register) only in the
		 * single-byte mode.
		 * In FIFO mode, NACK is set via LAST bit in the SMBnTXF_CTL
		 * register.
		 */
		else if ((p_status->flags & I2C_XFER_STOP) &&
			 !IS_ENABLED(NPCX_I2C_FIFO_SUPPORT)) {
			I2C_NACK(controller);
		}

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
				controller, p_status->oper_state,
				p_status->err_code);
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
	i2c_controller_int_handler(controller);
}

static void i2c0_interrupt(void)
{
	handle_interrupt(0);
}
static void i2c1_interrupt(void)
{
	handle_interrupt(1);
}
static void i2c2_interrupt(void)
{
	handle_interrupt(2);
}
static void i2c3_interrupt(void)
{
	handle_interrupt(3);
}
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
static void i2c4_interrupt(void)
{
	handle_interrupt(4);
}
static void i2c5_interrupt(void)
{
	handle_interrupt(5);
}
static void i2c6_interrupt(void)
{
	handle_interrupt(6);
}
static void i2c7_interrupt(void)
{
	handle_interrupt(7);
}
#endif

DECLARE_IRQ(NPCX_IRQ_SMB1, i2c0_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB2, i2c1_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB3, i2c2_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB4, i2c3_interrupt, 4);
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
DECLARE_IRQ(NPCX_IRQ_SMB5, i2c4_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB6, i2c5_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB7, i2c6_interrupt, 4);
DECLARE_IRQ(NPCX_IRQ_SMB8, i2c7_interrupt, 4);
#endif

/*****************************************************************************/
/* IC specific low-level driver */

void i2c_set_timeout(int port, uint32_t timeout)
{
	int ctrl = i2c_port_to_controller(port);

	/* Return if i2c_port_to_controller() returned an error */
	if (ctrl < 0)
		return;

	/* Param is port, but timeout is stored by-controller. */
	i2c_stsobjs[ctrl].timeout_us = timeout ? timeout :
						 I2C_TIMEOUT_DEFAULT_US;
}

int chip_i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
		  int out_size, uint8_t *in, int in_size, int flags)
{
	volatile struct i2c_status *p_status;
	int ctrl = i2c_port_to_controller(port);

	/* Return error if i2c_port_to_controller() returned an error */
	if (ctrl < 0)
		return EC_ERROR_INVAL;

	/* Skip unnecessary transaction */
	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	p_status = i2c_stsobjs + ctrl;

	/* Assign current task ID */
	p_status->task_waiting = task_get_current();

	/* Select port for multi-ports i2c controller */
	i2c_select_port(port);

	/* Copy data to controller struct */
	p_status->flags = flags;
	p_status->tx_buf = out;
	p_status->sz_txbuf = out_size;
	p_status->rx_buf = in;
	p_status->sz_rxbuf = in_size;
	p_status->addr_flags = addr_flags;

	/* Reset index & error */
	p_status->idx_buf = 0;
	p_status->err_code = SMB_OK;

	/* Make sure we're in a good state to start */
	if ((flags & I2C_XFER_START) &&
	    /* Ignore busy bus for repeated start */
	    p_status->oper_state != SMB_WRITE_SUSPEND &&
	    (i2c_bus_busy(ctrl) ||
	     (i2c_get_line_levels(port) != I2C_LINE_IDLE))) {
		int ret;

		/* Attempt to unwedge the i2c port */
		ret = i2c_unwedge(port);
		if (ret)
			return ret;
		p_status->err_code = SMB_BUS_BUSY;
		/* recover i2c controller */
		i2c_recovery(ctrl, p_status);
		/* Select port again for recovery */
		i2c_select_port(port);
	}

	CPUTS("\n");

	/* Start controller transaction */
	i2c_controller_transaction(ctrl);

	/* Reset task ID */
	p_status->task_waiting = TASK_ID_INVALID;

	CPRINTS("-Err:0x%02x", p_status->err_code);

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
			return IS_BIT_SET(
				NPCX_SMBCTL3(i2c_port_to_controller(port)),
				NPCX_SMBCTL3_SCL_LVL);
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
			return IS_BIT_SET(
				NPCX_SMBCTL3(i2c_port_to_controller(port)),
				NPCX_SMBCTL3_SDA_LVL);
	}

	/* If no SDA pin defined for this port, then return 1 to appear idle */
	return 1;
}

/*****************************************************************************/

static void i2c_port_set_freq(const int ctrl, const int bus_freq_kbps)
{
	int freq, j;
	int scl_freq;
	const struct i2c_timing *pTiming;
	int i2c_timing_used;

#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	/*
	 * SMB0/1/4/5/6/7 use APB3 clock
	 * SMB2/3 use APB2 clock
	 */
	freq = (ctrl < 2 || ctrl > 3) ? clock_get_apb3_freq() :
					clock_get_apb2_freq();
#else /* CHIP_FAMILY_NPCX5 */
	/*
	 * SMB0/1 use core clock
	 * SMB2/3 use APB2 clock
	 */
	freq = (ctrl < 2) ? clock_get_freq() : clock_get_apb2_freq();
#endif

	if (bus_freq_kbps == i2c_stsobjs[ctrl].kbps)
		return;

	/*
	 * Set SCL frequency by formula:
	 * tSCL = 4 * SCLFRQ * tCLK
	 * fSCL = fCLK / (4*SCLFRQ)
	 * SCLFRQ = ceil(fCLK/(4*fSCL))
	 */
	scl_freq = DIV_ROUND_UP(freq, bus_freq_kbps * 4000); /* Unit in bps */

	/* Normal mode if I2C freq is under 100kHz */
	if (bus_freq_kbps <= 100) {
		i2c_stsobjs[ctrl].kbps = bus_freq_kbps;
		/* Set divider value of SCL */
		SET_FIELD(NPCX_SMBCTL2(ctrl), NPCX_SMBCTL2_SCLFRQ7_FIELD,
			  (scl_freq & 0x7F));
		SET_FIELD(NPCX_SMBCTL3(ctrl), NPCX_SMBCTL3_SCLFRQ2_FIELD,
			  (scl_freq >> 7));
		return;
	}

	/* use Fast Mode */
	SET_BIT(NPCX_SMBCTL3(ctrl), NPCX_SMBCTL3_400K);
	/*
	 * Set SCLH(L)T and hold-time directly for best I2C
	 * timing condition for all source clocks. Please refer
	 * Section 7.5.9 "SMBus Timing - Fast Mode" for detail.
	 */
	if (bus_freq_kbps == 400) {
		pTiming = i2c_400k_timings;
		i2c_timing_used = i2c_400k_timing_used;
	} else if (bus_freq_kbps == 1000) {
		pTiming = i2c_1m_timings;
		i2c_timing_used = i2c_1m_timing_used;
	} else {
		i2c_stsobjs[ctrl].kbps = bus_freq_kbps;
		/* Set value from formula */
		NPCX_SMBSCLLT(ctrl) = scl_freq;
		NPCX_SMBSCLHT(ctrl) = scl_freq;
		cprints(CC_I2C,
			"Warning: I2C %d: Use 400kHz or 1MHz for better timing",
			ctrl);
		return;
	}

	for (j = 0; j < i2c_timing_used; j++, pTiming++) {
		if (pTiming->clock == (freq / SECOND)) {
			i2c_stsobjs[ctrl].kbps = bus_freq_kbps;
			/* Set SCLH(L)T and hold-time */
			NPCX_SMBSCLLT(ctrl) = pTiming->k1 / 2;
			NPCX_SMBSCLHT(ctrl) = pTiming->k2 / 2;
			SET_FIELD(NPCX_SMBCTL4(ctrl), NPCX_SMBCTL4_HLDT_FIELD,
				  pTiming->HLDT);
			break;
		}
	}
	if (j == i2c_timing_used)
		cprints(CC_I2C, "Error: I2C %d: src clk %d not supported", ctrl,
			freq / SECOND);
}

/* Hooks */

static void i2c_freq_changed(void)
{
	int i;

	for (i = 0; i < I2C_CONTROLLER_COUNT; ++i) {
		/* No bus speed configured */
		i2c_stsobjs[i].kbps = 0;
	}

	for (i = 0; i < i2c_ports_used; i++) {
		const struct i2c_port_t *p;
		int ctrl;

		p = &i2c_ports[i];
		ctrl = i2c_port_to_controller(p->port);
		if (ctrl < 0)
			continue;
		i2c_port_set_freq(ctrl, p->kbps);
	}
}

DECLARE_HOOK(HOOK_FREQ_CHANGE, i2c_freq_changed, HOOK_PRIO_DEFAULT);

enum i2c_freq chip_i2c_get_freq(int chip_i2c_port)
{
	int ctrl;
	int kbps;

	ctrl = i2c_port_to_controller(chip_i2c_port);
	if (ctrl < 0)
		return I2C_FREQ_COUNT;

	kbps = i2c_stsobjs[ctrl].kbps;

	if (kbps > 400)
		return I2C_FREQ_1000KHZ;
	if (kbps > 100)
		return I2C_FREQ_400KHZ;

	if (kbps == 100)
		return I2C_FREQ_100KHZ;

	return I2C_FREQ_COUNT;
}

int chip_i2c_set_freq(int chip_i2c_port, enum i2c_freq freq)
{
	int ctrl;
	int bus_freq_kbps;

	ctrl = i2c_port_to_controller(chip_i2c_port);
	if (ctrl < 0)
		return EC_ERROR_INVAL;

	switch (freq) {
	case I2C_FREQ_100KHZ:
		bus_freq_kbps = 100;
		break;
	case I2C_FREQ_400KHZ:
		bus_freq_kbps = 400;
		break;
	case I2C_FREQ_1000KHZ:
		bus_freq_kbps = 1000;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	i2c_port_set_freq(ctrl, bus_freq_kbps);
	return EC_SUCCESS;
}

void i2c_init(void)
{
	int i;

	/* Configure pins from GPIOs to I2Cs */
	gpio_config_module(MODULE_I2C, 1);

	/* Enable clock for I2C peripheral */
	clock_enable_peripheral(CGC_OFFSET_I2C, CGC_I2C_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7
	clock_enable_peripheral(CGC_OFFSET_I2C2, CGC_I2C_MASK2,
				CGC_MODE_RUN | CGC_MODE_SLEEP);
#endif

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
