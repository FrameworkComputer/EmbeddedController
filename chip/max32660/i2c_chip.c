/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 I2C port module for Chrome EC. */

#include <stdint.h>
#include <stddef.h>
#include "common.h"
#include "config_chip.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "registers.h"
#include "i2c_regs.h"

/**
 * Byte to use if the EC HOST requested more data
 * than the I2C Slave is able to send.
 */
#define EC_PADDING_BYTE 0xec

/* **** Definitions **** */
#define I2C_ERROR \
	(MXC_F_I2C_INT_FL0_ARB_ER | MXC_F_I2C_INT_FL0_TO_ER | \
	 MXC_F_I2C_INT_FL0_ADDR_NACK_ER | MXC_F_I2C_INT_FL0_DATA_ER | \
	 MXC_F_I2C_INT_FL0_DO_NOT_RESP_ER | MXC_F_I2C_INT_FL0_START_ER | \
	 MXC_F_I2C_INT_FL0_STOP_ER)

#define T_LOW_MIN (160) /* tLOW minimum in nanoseconds */
#define T_HIGH_MIN (60) /* tHIGH minimum in nanoseconds */
#define T_R_MAX_HS (40) /* tR maximum for high speed mode in nanoseconds */
#define T_F_MAX_HS (40) /* tF maximum for high speed mode in nanoseconds */
#define T_AF_MIN (10)	/* tAF minimun in nanoseconds */

/**
 * typedef i2c_speed_t - I2C speed modes.
 * @I2C_STD_MODE: 100KHz bus speed
 * @I2C_FAST_MODE: 400KHz Bus Speed
 * @I2C_FASTPLUS_MODE: 1MHz Bus Speed
 * @I2C_HS_MODE: 3.4MHz Bus Speed
 */
typedef enum {
	I2C_STD_MODE = 100000,
	I2C_FAST_MODE = 400000,
	I2C_FASTPLUS_MODE = 1000000,
	I2C_HS_MODE = 3400000
} i2c_speed_t;

/**
 * typedef i2c_autoflush_disable_t - Enable/Disable TXFIFO Autoflush mode.
 */
typedef enum {
	I2C_AUTOFLUSH_ENABLE = 0,
	I2C_AUTOFLUSH_DISABLE = 1
} i2c_autoflush_disable_t;

/**
 * typedef i2c_master_state_t - Available transaction states for I2C Master.
 */
typedef enum {
	I2C_MASTER_IDLE = 1,
	I2C_MASTER_START = 2,
	I2C_MASTER_WRITE_COMPLETE = 3,
	I2C_MASTER_READ_COMPLETE = 4
} i2c_master_state_t;

/**
 * typedef i2c_slave_state_t - Available transaction states for I2C Slave.
 */
typedef enum {
	I2C_SLAVE_WRITE_COMPLETE = 0,
	I2C_SLAVE_ADDR_MATCH_READ = 1,
	I2C_SLAVE_ADDR_MATCH_WRITE = 2,
} i2c_slave_state_t;

/**
 * typedef i2c_req_t - I2C Transaction request.
 */
typedef struct i2c_req i2c_req_t;

/**
 * typedef i2c_req_state_t - Saves the state of the non-blocking requests.
 * @req: Pointer to I2C transaction request information.
 */
typedef struct {
	i2c_req_t *req;
} i2c_req_state_t;

/**
 * struct i2c_req - I2C Transaction request.
 * @addr: I2C 7-bit Address right aligned, bit 6 to bit 0.
 * 	  Only supports 7-bit addressing. LSb of the given
 * 	  address will be used as the read/write bit, the addr
 * 	  will not be shifted. Used for both master and slave
 * 	  transactions.
 * @addr_match_flag: Indicates which slave address was matched.
 *                   0x1 indicates first slave address matched.
 *                   0x2 indicates second slave address matched.
 *                   0x4 indicates third slave address matched.
 *                   0x8 indicates fourth slave address matched.
 * @tx_data: Data for master write/slave read.
 * @rx_data: Data for master read/slave write.
 * @received_count:  Number of rx bytes sent.
 * @tx_remain: Number of bytes to transmit to the master. This
 *             value is -1 if should clock stretch, 0 if start
 *             sending EC_PADDING_BYTE.  Any other values in this
 *             field will transmit data to the Master.
 * @restart: Restart or stop bit indicator.
 *           0 to send a stop bit at the end of the transaction
 *           Non-zero to send a restart at end of the transaction
 *           Only used for Master transactions.
 * @callback: Callback for asynchronous request.
 *            First argument is to the transaction request.
 *            Second argument is the error code.
 */
struct i2c_req {
	uint8_t addr;
	uint8_t addr_match_flag;
	const uint8_t *tx_data;
	uint8_t *rx_data;
	volatile unsigned received_count;
	volatile int tx_remain;
	volatile i2c_slave_state_t state;
	volatile int restart;
};

static i2c_req_state_t states[MXC_I2C_INSTANCES];

/**
 * struct i2c_port_data
 * @out: Output data pointer.
 * @out_size: Output data to transfer, in bytes.
 * @in: Input data pointer.
 * @in_size: Input data to transfer, in bytes.
 * @flags: Flags (I2C_XFER_*).
 * @idx: Index into input/output data.
 * @err: Error code, if any.
 * @timeout_us:	Transaction timeout, or 0 to use default.
 * @task_waiting: Task waiting on port, or TASK_ID_INVALID if none.
 */
struct i2c_port_data {
	const uint8_t *out;
	int out_size;
	uint8_t *in;
	int in_size;
	int flags;
	int idx;
	int err;
	uint32_t timeout_us;
	volatile int task_waiting;
};
static struct i2c_port_data pdata[I2C_PORT_COUNT];

/* **** Function Prototypes **** */
static int i2c_init_peripheral(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed);
static int i2c_master_write(mxc_i2c_regs_t *i2c, uint8_t addr, int start,
			    int stop, const uint8_t *data, int len,
			    int restart);
static int i2c_master_read(mxc_i2c_regs_t *i2c, uint8_t addr, int start,
			   int stop, uint8_t *data, int len, int restart);

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS
static void init_i2cs(int port);
static int i2c_slave_async(mxc_i2c_regs_t *i2c, i2c_req_t *req);
static void i2c_slave_handler(mxc_i2c_regs_t *i2c);
#endif /* CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS */

/* Port address for each I2C */
static mxc_i2c_regs_t *i2c_bus_ports[] = {MXC_I2C0, MXC_I2C1};

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS

#ifdef CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS
static void i2c_send_board_response(int len);
static void i2c_process_board_command(int read, int addr, int len);
void board_i2c_process(int read, uint8_t addr, int len, char *buffer,
		       void (*send_response)(int len));
#endif /* CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS */
#endif /* CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS */

/**
 * chip_i2c_xfer() - Low Level function for I2C Master Reads and Writes.
 * @port: Port to access
 * @slave_addr:	Slave device address
 * @out: Data to send
 * @out_size: Number of bytes to send
 * @in: Destination buffer for received data
 * @in_size: Number of bytes to receive
 * @flags: Flags (see I2C_XFER_* above)
 *
 * Chip-level function to transmit one block of raw data, then receive one
 * block of raw data.
 *
 * This is a low-level chip-dependent function and should only be called by
 * i2c_xfer().\
 *
 * Return EC_SUCCESS, or non-zero if error.
 */
int chip_i2c_xfer(int port, const uint16_t slave_addr_flags, const uint8_t *out,
		  int out_size, uint8_t *in, int in_size, int flags)
{
	int xfer_start;
	int xfer_stop;
	int status;

	xfer_start = flags & I2C_XFER_START;
	xfer_stop = flags & I2C_XFER_STOP;

	if (out_size) {
		status = i2c_master_write(i2c_bus_ports[port], slave_addr_flags,
					  xfer_start, xfer_stop, out, out_size,
					  1);
		if (status != EC_SUCCESS) {
			return status;
		}
	}
	if (in_size) {
		status = i2c_master_read(i2c_bus_ports[port], slave_addr_flags,
					 xfer_start, xfer_stop, in, in_size, 0);
		if (status != EC_SUCCESS) {
			return status;
		}
	}
	return EC_SUCCESS;
}

/**
 * i2c_get_line_levels() - Read the current digital levels on the I2C pins.
 * @port: Port number to use when reading line levels.
 *
 * Return a byte where bit 0 is the line level of SCL and
 * bit 1 is the line level of SDA.
 */
int i2c_get_line_levels(int port)
{
	/* Retrieve the current levels of SCL and SDA from the control reg. */
	return (i2c_bus_ports[port]->ctrl >> MXC_F_I2C_CTRL_SCL_POS) & 0x03;
}

/**
 * i2c_set_timeout()
 * @port: Port number to set timeout for.
 * @timeout: Timeout duration in microseconds.
 */
void i2c_set_timeout(int port, uint32_t timeout)
{
	pdata[port].timeout_us = timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

/**
 * i2c_init() - Initialize the I2C ports used on device.
 */
void i2c_init(void)
{
	int i;
	int port;

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Initialize all I2C ports used. */
	for (i = 0; i < i2c_ports_used; i++) {
		port = i2c_ports[i].port;
		i2c_init_peripheral(i2c_bus_ports[port],
				    i2c_ports[i].kbps * 1000);
		i2c_set_timeout(i, 0);
	}

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS
	/* Initialize the I2C Slave */
	init_i2cs(I2C_PORT_EC);
#ifdef CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS
	/*
	 * Set the secondary I2C slave address for the board.
	 */
	/* Index the secondary slave address. */
	i2c_bus_ports[I2C_PORT_EC]->slave_addr =
		(i2c_bus_ports[I2C_PORT_EC]->slave_addr &
		 ~(MXC_F_I2C_SLAVE_ADDR_SLAVE_ADDR_IDX |
		   MXC_F_I2C_SLAVE_ADDR_SLAVE_ADDR_DIS)) |
		(1 << MXC_F_I2C_SLAVE_ADDR_SLAVE_ADDR_IDX_POS);
	/* Set the secondary slave address. */
	i2c_bus_ports[I2C_PORT_EC]->slave_addr =
		(1 << MXC_F_I2C_SLAVE_ADDR_SLAVE_ADDR_IDX_POS) |
		CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS;
#endif /* CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS */
#endif /* CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS */

}

/**
 *  I2C Slave Implentation
 */
#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS
/* IRQ for each I2C */
static uint32_t i2c_bus_irqs[] = {EC_I2C0_IRQn, EC_I2C1_IRQn};

/**
 * Buffer for received host command packets (including prefix byte on request,
 * and result/size on response).  After any protocol-specific headers, the
 * buffers must be 32-bit aligned.
 */
static uint8_t host_buffer_padded[I2C_MAX_HOST_PACKET_SIZE + 4 +
				  CONFIG_I2C_EXTRA_PACKET_SIZE] __aligned(4);
static uint8_t *const host_buffer = host_buffer_padded + 2;
static uint8_t params_copy[I2C_MAX_HOST_PACKET_SIZE] __aligned(4);
static struct host_packet i2c_packet;

static i2c_req_t req_slave;
volatile int ec_pending_response = 0;

/**
 * i2c_send_response_packet() - Send the response packet to get processed.
 * @pkt: Packet to send.
 */
static void i2c_send_response_packet(struct host_packet *pkt)
{
	int size = pkt->response_size;
	uint8_t *out = host_buffer;

	/* Ignore host command in-progress. */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result and size to first two bytes. */
	*out++ = pkt->driver_result;
	*out++ = size;

	/* Host_buffer data range. */
	req_slave.tx_remain = size + 2;

	/* Call the handler to send the response packet. */
	i2c_slave_handler(i2c_bus_ports[I2C_PORT_EC]);
}

/**
 * i2c_process_command() - Process the command in the i2c host buffer.
 */
static void i2c_process_command(void)
{
	char *buff = host_buffer;

	i2c_packet.send_response = i2c_send_response_packet;
	i2c_packet.request = (const void *)(&buff[1]);
	i2c_packet.request_temp = params_copy;
	i2c_packet.request_max = sizeof(params_copy);
	/* Don't know the request size so pass in the entire buffer. */
	i2c_packet.request_size = I2C_MAX_HOST_PACKET_SIZE;

	/*
	 * Stuff response at buff[2] to leave the first two bytes of
	 * buffer available for the result and size to send over i2c.  Note
	 * that this 2-byte offset and the 2-byte offset from host_buffer
	 * add up to make the response buffer 32-bit aligned.
	 */
	i2c_packet.response = (void *)(&buff[2]);
	i2c_packet.response_max = I2C_MAX_HOST_PACKET_SIZE;
	i2c_packet.response_size = 0;

	if (*buff >= EC_COMMAND_PROTOCOL_3) {
		i2c_packet.driver_result = EC_RES_SUCCESS;
	} else {
		/* Only host command protocol 3 is supported. */
		i2c_packet.driver_result = EC_RES_INVALID_HEADER;
	}

	host_packet_receive(&i2c_packet);
}

/**
 * i2c_slave_service() - Called by the I2C slave interrupt controller.
 * @req:   Request currently being processed.
 */
void i2c_slave_service(i2c_req_t *req)
{
	/* Check if there was a host command (I2C master write). */
	if (req->state == I2C_SLAVE_ADDR_MATCH_WRITE) {
		req->state = I2C_SLAVE_WRITE_COMPLETE;

#ifdef CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS
		if (req->addr_match_flag != 0x1) {
			i2c_process_board_command(
				1, CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS,
				req->received_count);
		} else
#endif /* CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS */
			i2c_process_command();
	}
}

/**
 * I2C0_IRQHandler() - Async Handler for I2C Slave driver.
 */
void I2C0_IRQHandler(void)
{
	i2c_slave_handler(i2c_bus_ports[0]);
}

/**
 * I2C1_IRQHandler() - Async Handler for I2C Slave driver.
 */
void I2C1_IRQHandler(void)
{
	i2c_slave_handler(i2c_bus_ports[1]);
}

DECLARE_IRQ(EC_I2C0_IRQn, I2C0_IRQHandler, 1);
DECLARE_IRQ(EC_I2C1_IRQn, I2C1_IRQHandler, 1);

/**
 * i2c_slave_service_read() - Services the Master I2C read from the slave.
 * @i2c: I2C peripheral pointer.
 * @req: Pointer to the request info.
 */
static void i2c_slave_service_read(mxc_i2c_regs_t *i2c, i2c_req_t *req)
{
	/* Clear the RX Threshold interrupt if set. */
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;
	/* Clear the TX Threshold interrupt if set. */
	if (i2c->int_fl0 & MXC_F_I2C_INT_FL0_TX_THRESH) {
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_TX_THRESH;
	}
	/**
	 * If there is nothing to transmit to the EC HOST, then default
	 * to clock stretching.
	 */
	if (req->tx_remain < 0) {
		return;
	}
	/* If there is data to send to the Master then fill the TX FIFO. */
	if (req->tx_remain != 0) {
		/* Fill the FIFO with data to transimit to the I2C Master. */
		while ((req->tx_remain > 0) &&
			!(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
			i2c->fifo = *(req->tx_data)++;
			req->tx_remain--;
		}
	}
	/*
	 * If we have sent everything to the Master that we can,
	 * then send padding byte.
	 */
	if (req->tx_remain == 0) {
		/* Fill the FIFO with the EC padding byte. */
		while (!(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
			i2c->fifo = EC_PADDING_BYTE;
		}
	}
	/* Set the threshold for TX, the threshold is a four bit field. */
	i2c->tx_ctrl1 = ((i2c->tx_ctrl1 & ~(MXC_F_I2C_TX_CTRL0_TX_THRESH)) |
			(2 << MXC_F_I2C_TX_CTRL0_TX_THRESH_POS));
	/* Enable TX Threshold, Done and Error interrupts. */
	i2c->int_en0 = MXC_F_I2C_INT_EN0_TX_THRESH | MXC_F_I2C_INT_EN0_DONE |
			I2C_ERROR;
}

/**
 * i2c_slave_service_write() - Services the Master I2C write to the slave.
 * @i2c: I2C peripheral pointer.
 * @req: Pointer to the request info.
 */
static void i2c_slave_service_write(mxc_i2c_regs_t *i2c, i2c_req_t *req)
{
	/* Clear the RX Threshold interrupt if set. */
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;
	/* Read out any data in the RX FIFO. */
	while (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
		*(req->rx_data)++ = i2c->fifo;
		req->received_count++;
	}
	/* Set the RX threshold interrupt level. */
	i2c->rx_ctrl0 = ((i2c->rx_ctrl0 &
			~(MXC_F_I2C_RX_CTRL0_RX_THRESH)) |
			(MXC_I2C_FIFO_DEPTH - 1)
				<< MXC_F_I2C_RX_CTRL0_RX_THRESH_POS);
	/* Enable RXTH interrupt and Error interrupts. */
	i2c->int_en0 = MXC_F_I2C_INT_EN0_RX_THRESH | MXC_F_I2C_INT_EN0_DONE | I2C_ERROR;
}

/**
 * i2c_slave_handler() - I2C interrupt handler.
 * @i2c: Base address of the I2C module.
 *
 * This function should be called by the application from the interrupt
 * handler if I2C interrupts are enabled. Alternately, this function
 * can be periodically called by the application if I2C interrupts are
 * disabled.
 */
static void i2c_slave_handler(mxc_i2c_regs_t *i2c)
{
	i2c_req_t *req;

	/* Get the request context for this interrupt. */
	req = states[MXC_I2C_GET_IDX(i2c)].req;

	/* Check for DONE interrupt. */
	if (i2c->int_fl0 & MXC_F_I2C_INT_FL0_DONE) {
		/* Clear all interrupts except a possible address match. */
		i2c->int_fl0 = i2c->int_fl0 & ~MXC_F_I2C_INT_FL0_ADDR_MATCH;
		i2c->int_fl1 = i2c->int_fl1;

		/* Disable all interrupts except address match. */
		i2c->int_en1 = 0;
		i2c->int_en0 = MXC_F_I2C_INT_EN0_ADDR_MATCH;

		if (req->state == I2C_SLAVE_ADDR_MATCH_WRITE) {
			/* Read out any data in the RX FIFO. */
			while (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
				*(req->rx_data)++ = i2c->fifo;
				req->received_count++;
			}
		}
		/* Manually clear the TXFIFO. */
		i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH;

		/* Process the Master write that just finished. */
		i2c_slave_service(req);

		/* No longer inhibit deep sleep after done. */
		enable_sleep(SLEEP_MASK_I2C_SLAVE);
	}

	/* Check for an address match. */
	if (i2c->int_fl0 & MXC_F_I2C_INT_FL0_ADDR_MATCH) {
		/*
		 * Save the address match index to identify
		 * targeted slave address.
		 */
		req->addr_match_flag =
			(i2c->int_fl0 & MXC_F_I2C_INT_FL0_MAMI_MASK) >>
			MXC_F_I2C_INT_FL0_MAMI_POS;

		/* Check if Master is writing to the slave. */
		if (!(i2c->ctrl & MXC_F_I2C_CTRL_READ)) {
			/* I2C Master is writing to the slave. */
			req->rx_data = host_buffer;
			req->tx_data = host_buffer;
			req->tx_remain = -1; /* Nothing to send yet. */
			/* Clear the RX (receive from I2C Master) byte counter. */
			req->received_count = 0;
			req->state = I2C_SLAVE_ADDR_MATCH_WRITE;
		} else {
			/* The Master is reading from the slave. */
			/* Start transmitting to the Master from the start of buffer. */
			req->tx_data = host_buffer;
			req->state = I2C_SLAVE_ADDR_MATCH_READ;
		}

		/* Clear all interrupt flags. */
		i2c->int_fl0 = i2c->int_fl0;
		i2c->int_fl1 = i2c->int_fl1;

		/* Respond to the DONE interrupt. */
		i2c->int_en0 = MXC_F_I2C_INT_EN0_DONE;
		/* Inhibit sleep mode when addressed until STOPF flag is set. */
		disable_sleep(SLEEP_MASK_I2C_SLAVE);
	}

	if (i2c->int_fl0 & I2C_ERROR) {
		/* Clear the error interrupt. */
		i2c->int_fl0 = I2C_ERROR;
		i2c->int_en0 = 0;
		/* Manually clear the TXFIFO. */
		i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH;
		/* Disable and clear interrupts. */
		i2c->int_en0 = 0;
		i2c->int_en1 = 0;
		i2c->int_fl0 = i2c->int_fl0;
		i2c->int_fl1 = i2c->int_fl1;
		/* Cycle the I2C peripheral enable on error. */
		i2c->ctrl = 0;
		i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN;
		return;
	}

	/* Check for an I2C Master Read or Write. */
	if (req->state == I2C_SLAVE_ADDR_MATCH_READ) {
		/* Service a read request from the I2C Master. */
		i2c_slave_service_read(i2c, req);
		return;
	}
	if (req->state == I2C_SLAVE_ADDR_MATCH_WRITE) {
		/* Service a write request from the I2C Master. */
		i2c_slave_service_write(i2c, req);
		return;
	}
}

/**
 * init_i2cs() - Async Handler for I2C Slave driver.
 * @port: I2C port number to initialize.
 */
void init_i2cs(int port)
{
	int error;

	error = i2c_init_peripheral(i2c_bus_ports[port], I2C_STD_MODE);
	if (error != EC_SUCCESS) {
		while (1)
			;
	}
	/* Prepare for interrupt driven slave requests. */
	req_slave.addr = CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS;
	req_slave.tx_data = host_buffer; /* Transmitted to host. */
	req_slave.tx_remain = -1;
	req_slave.rx_data = host_buffer; /* Received from host. */
	req_slave.restart = 0;
	states[port].req = &req_slave;
	error = i2c_slave_async(i2c_bus_ports[port], &req_slave);
	if (error != EC_SUCCESS) {
		while (1)
			;
	}

	task_enable_irq(i2c_bus_irqs[port]);
}

/**
 * i2c_slave_async() - Slave Read and Write Asynchronous.
 * @i2c:   Pointer to I2C regs.
 * @req:   Request for an I2C transaction.
 *
 * Return EC_SUCCESS if successful, otherwise returns a common error code.
 */
static int i2c_slave_async(mxc_i2c_regs_t *i2c, i2c_req_t *req)
{
	/* Make sure the I2C has been initialized. */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN))
		return EC_ERROR_UNKNOWN;
	/* Disable master mode. */
	i2c->ctrl &= ~(MXC_F_I2C_CTRL_MST);
	/* Set the Slave Address in the I2C peripheral register. */
	i2c->slave_addr = req->addr;
	/* Clear the receive count from the I2C Master. */
	req->received_count = 0;
	/* Disable and clear the interrupts. */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;
	/* Only enable the I2C Address match interrupt. */
	i2c->int_en0 = MXC_F_I2C_INT_EN0_ADDR_MATCH;

	return EC_SUCCESS;
}

#ifdef CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS

static void i2c_send_board_response(int len)
{
	/* Set the number of bytes to send to the I2C master. */
	req_slave.tx_remain = len;

	/* Call the handler for transmition of response packet. */
	i2c_slave_handler(i2c_bus_ports[I2C_PORT_EC]);
}


static void i2c_process_board_command(int read, int addr, int len)
{
	board_i2c_process(read, addr, len, &host_buffer[0],
			  i2c_send_board_response);
}
#endif /* CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS */
#endif /* CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS */

/**
 * i2c_set_speed() - Set the transfer speed of the selected I2C.
 * @i2c: Pointer to I2C peripheral.
 * @i2cspeed: Speed to set.
 *
 * Return EC_SUCCESS, or non-zero if error.
 */
static int i2c_set_speed(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed)
{
	uint32_t ticks;
	uint32_t ticks_lo;
	uint32_t ticks_hi;
	uint32_t time_pclk;
	uint32_t target_bus_freq;
	uint32_t time_scl_min;
	uint32_t clock_low_min;
	uint32_t clock_high_min;
	uint32_t clock_min;

	if (i2cspeed == I2C_HS_MODE) {
		/* Compute dividers for high speed mode. */
		time_pclk = 1000000 / (PeripheralClock / 1000);

		target_bus_freq = i2cspeed;
		if (target_bus_freq < 1000) {
			return EC_ERROR_INVAL;
		}

		time_scl_min = 1000000 / (target_bus_freq / 1000);
		clock_low_min =
			((T_LOW_MIN + T_F_MAX_HS + (time_pclk - 1) - T_AF_MIN) /
			 time_pclk) - 1;
		clock_high_min = ((T_HIGH_MIN + T_R_MAX_HS + (time_pclk - 1) -
				  T_AF_MIN) /
				  time_pclk) - 1;
		clock_min = ((time_scl_min + (time_pclk - 1)) / time_pclk) - 2;

		ticks_lo = (clock_low_min > (clock_min - clock_high_min))
				   ? (clock_low_min)
				   : (clock_min - clock_high_min);
		ticks_hi = clock_high_min;

		if ((ticks_lo > (MXC_F_I2C_HS_CLK_HS_CLK_LO >>
				 MXC_F_I2C_HS_CLK_HS_CLK_LO_POS)) ||
		    (ticks_hi > (MXC_F_I2C_HS_CLK_HS_CLK_HI >>
				 MXC_F_I2C_HS_CLK_HS_CLK_HI_POS))) {
			return EC_ERROR_INVAL;
		}

		/* Write results to destination registers. */
		i2c->hs_clk = (ticks_lo << MXC_F_I2C_HS_CLK_HS_CLK_LO_POS) |
			      (ticks_hi << MXC_F_I2C_HS_CLK_HS_CLK_HI_POS);

		/* Still need to load dividers for the preamble that each
		 * high-speed transaction starts with. Switch setting to fast
		 * mode and fall out of if statement.
		 */
		i2cspeed = I2C_FAST_MODE;
	}

	/* Get the number of periph clocks needed to achieve selected speed. */
	ticks = PeripheralClock / i2cspeed;

	/* For a 50% duty cycle, half the ticks will be spent high and half will
	 * be low.
	 */
	ticks_hi = (ticks >> 1) - 1;
	ticks_lo = (ticks >> 1) - 1;

	/* Account for rounding error in odd tick counts. */
	if (ticks & 1) {
		ticks_hi++;
	}

	/* Will results fit into 9 bit registers?  (ticks_hi will always be >=
	 * ticks_lo.  No need to check ticks_lo.)
	 */
	if (ticks_hi > 0x1FF) {
		return EC_ERROR_INVAL;
	}

	/* 0 is an invalid value for the destination registers. (ticks_hi will
	 * always be >= ticks_lo.  No need to check ticks_hi.)
	 */
	if (ticks_lo == 0) {
		return EC_ERROR_INVAL;
	}

	/* Write results to destination registers. */
	i2c->clk_lo = ticks_lo;
	i2c->clk_hi = ticks_hi;

	return EC_SUCCESS;
}

/**
 * i2c_init_peripheral() - Initialize and enable I2C.
 * @i2c:      Pointer to I2C peripheral registers.
 * @i2cspeed: Desired speed (I2C mode).
 * @sys_cfg:  System configuration object.
 *
 * Return EC_SUCCESS, or non-zero if error.
 */
static int i2c_init_peripheral(mxc_i2c_regs_t *i2c, i2c_speed_t i2cspeed)
{
	/**
	 * Always disable the HW autoflush on data NACK and let the SW handle
	 * the flushing.
	 */
	i2c->tx_ctrl0 |= 0x20;

	i2c->ctrl = 0; /* Clear configuration bits. */
	i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN; /* Enable I2C. */
	i2c->master_ctrl = 0; /* Clear master configuration bits. */
	i2c->status = 0; /* Clear status bits. */

	i2c->ctrl = 0; /* Clear configuration bits. */
	i2c->ctrl = MXC_F_I2C_CTRL_I2C_EN; /* Enable I2C. */
	i2c->master_ctrl = 0; /* Clear master configuration bits. */
	i2c->status = 0; /* Clear status bits. */

	/* Check for HS mode. */
	if (i2cspeed == I2C_HS_MODE) {
		i2c->ctrl |= MXC_F_I2C_CTRL_HS_MODE; /* Enable HS mode. */
	}

	/* Disable and clear interrupts. */
	i2c->int_en0 = 0;
	i2c->int_en1 = 0;
	i2c->int_fl0 = i2c->int_fl0;
	i2c->int_fl1 = i2c->int_fl1;

	i2c->timeout = 0x0; /* Set timeout. */
	i2c->rx_ctrl0 |= MXC_F_I2C_RX_CTRL0_RX_FLUSH; /* Clear the RX FIFO. */
	i2c->tx_ctrl0 |= MXC_F_I2C_TX_CTRL0_TX_FLUSH; /* Clear the TX FIFO. */

	return i2c_set_speed(i2c, i2cspeed);
}

/**
 * i2c_master_write()
 * @i2c:  Pointer to I2C regs.
 * @addr: I2C 7-bit Address left aligned, bit 7 to bit 1.
 *        Only supports 7-bit addressing. LSb of the given address
 *        will be used as the read/write bit, the \p addr <b>will
 *        not be shifted. Used for both master and
 *        slave transactions.
 * @data: Data to be written.
 * @len:  Number of bytes to Write.
 * @restart: 0 to send a stop bit at the end of the transaction,
 *	     otherwise send a restart.
 *
 * Will block until transaction is complete.
 *
 * Return  EC_SUCCESS, or non-zero if error.
 */
static int i2c_master_write(mxc_i2c_regs_t *i2c, uint8_t addr, int start,
			    int stop, const uint8_t *data, int len, int restart)
{
	if (len == 0) {
		return EC_SUCCESS;
	}

	/* Clear the interrupt flag. */
	i2c->int_fl0 = i2c->int_fl0;

	/* Make sure the I2C has been initialized. */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN)) {
		return EC_ERROR_UNKNOWN;
	}

	/* Enable master mode. */
	i2c->ctrl |= MXC_F_I2C_CTRL_MST;

	/* Load FIFO with slave address for WRITE and as much data as we can. */
	while (i2c->status & MXC_F_I2C_STATUS_TX_FULL) {
	}

	if (start) {
		/**
		 * The slave address is right-aligned, bits 6 to 0, shift
		 * to the left and make room for the write bit.
		 */
		i2c->fifo = (addr << 1) & ~(0x1);
	}

	while ((len > 0) && !(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
		i2c->fifo = *data++;
		len--;
	}
	/* Generate Start signal. */
	if (start) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_START;
	}

	/* Write remaining data to FIFO. */
	while (len > 0) {
		/* Check for errors. */
		if (i2c->int_fl0 & I2C_ERROR) {
			/* Set the stop bit. */
			i2c->master_ctrl &= ~(MXC_F_I2C_MASTER_CTRL_RESTART);
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
			return EC_ERROR_UNKNOWN;
		}

		if (!(i2c->status & MXC_F_I2C_STATUS_TX_FULL)) {
			i2c->fifo = *data++;
			len--;
		}
	}
	/* Check if Repeated Start requested. */
	if (restart) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_RESTART;
	} else {
		if (stop) {
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
		}
	}

	if (stop) {
		/* Wait for Done. */
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_DONE)) {
			/* Check for errors */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit */
				i2c->master_ctrl &=
					~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear Done interrupt flag. */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_DONE;
	}

	/* Wait for Stop if requested and there is no restart. */
	if (stop && !restart) {
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_STOP)) {
			/* Check for errors */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit */
				i2c->master_ctrl &=
					~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear stop interrupt flag. */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_STOP;
	}

	/* Check for errors. */
	if (i2c->int_fl0 & I2C_ERROR) {
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/**
 * i2c_master_read()
 * @i2c:        Pointer to I2C regs.
 * @addr:       I2C 7-bit Address right aligned, bit 6 to bit 0.
 * @data:       Data to be written.
 * @len:        Number of bytes to Write.
 * @restart:    0 to send a stop bit at the end of the transaction,
 * 	       otherwise send a restart.
 *
 * Will block until transaction is complete.
 *
 * Return:     EC_SUCCESS if successful, otherwise returns a common error code
 */
static int i2c_master_read(mxc_i2c_regs_t *i2c, uint8_t addr, int start,
			   int stop, uint8_t *data, int len, int restart)
{
	volatile int length = len;
	int interactive_receive_mode;

	if (len == 0) {
		return EC_SUCCESS;
	}

	if (len > 255) {
		return EC_ERROR_INVAL;
	}

	/* Clear the interrupt flag. */
	i2c->int_fl0 = i2c->int_fl0;

	/* Make sure the I2C has been initialized. */
	if (!(i2c->ctrl & MXC_F_I2C_CTRL_I2C_EN)) {
		return EC_ERROR_UNKNOWN;
	}

	/* Enable master mode. */
	i2c->ctrl |= MXC_F_I2C_CTRL_MST;

	if (stop) {
		/* Set receive count. */
		i2c->ctrl &= ~MXC_F_I2C_CTRL_RX_MODE;
		i2c->rx_ctrl1 = len;
		interactive_receive_mode = 0;
	} else {
		i2c->ctrl |= MXC_F_I2C_CTRL_RX_MODE;
		i2c->rx_ctrl1 = 1;
		interactive_receive_mode = 1;
	}

	/* Load FIFO with slave address. */
	if (start) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_START;
		while (i2c->status & MXC_F_I2C_STATUS_TX_FULL) {
		}
		/**
		 * The slave address is right-aligned, bits 6 to 0, shift
		 * to the left and make room for the read bit.
		 */
		i2c->fifo = ((addr << 1) | 1);
	}

	/* Wait for all data to be received or error. */
	while (length > 0) {
		/* Check for errors */
		if (i2c->int_fl0 & I2C_ERROR) {
			/* Set the stop bit. */
			i2c->master_ctrl &= ~(MXC_F_I2C_MASTER_CTRL_RESTART);
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
			return EC_ERROR_UNKNOWN;
		}

		/* If in interactive receive mode then ack each received byte. */
		if (interactive_receive_mode) {
			while (!(i2c->int_fl0 & MXC_F_I2C_INT_EN0_RX_MODE))
				;
			if (i2c->int_fl0 & MXC_F_I2C_INT_EN0_RX_MODE) {
				/* Read the data. */
				*data++ = i2c->fifo;
				length--;
				/* Clear the bit. */
				if (length != 1) {
					i2c->int_fl0 =
						MXC_F_I2C_INT_EN0_RX_MODE;
				}
			}
		} else {
			if (!(i2c->status & MXC_F_I2C_STATUS_RX_EMPTY)) {
				*data++ = i2c->fifo;
				length--;
			}
		}
	}

	if (restart) {
		i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_RESTART;
	} else {
		if (stop) {
			i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
		}
	}

	/* Wait for Done. */
	if (stop) {
		while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_DONE)) {
			/* Check for errors. */
			if (i2c->int_fl0 & I2C_ERROR) {
				/* Set the stop bit. */
				i2c->master_ctrl &=
					~(MXC_F_I2C_MASTER_CTRL_RESTART);
				i2c->master_ctrl |= MXC_F_I2C_MASTER_CTRL_STOP;
				return EC_ERROR_UNKNOWN;
			}
		}
		/* Clear Done interrupt flag. */
		i2c->int_fl0 = MXC_F_I2C_INT_FL0_DONE;
	}

	/* Wait for Stop. */
	if (!restart) {
		if (stop) {
			while (!(i2c->int_fl0 & MXC_F_I2C_INT_FL0_STOP)) {
				/* Check for errors. */
				if (i2c->int_fl0 & I2C_ERROR) {
					/* Set the stop bit. */
					i2c->master_ctrl &= ~(
						MXC_F_I2C_MASTER_CTRL_RESTART);
					i2c->master_ctrl |=
						MXC_F_I2C_MASTER_CTRL_STOP;
					return EC_ERROR_UNKNOWN;
				}
			}
			/* Clear Stop interrupt flag. */
			i2c->int_fl0 = MXC_F_I2C_INT_FL0_STOP;
		}
	}

	/* Check for errors. */
	if (i2c->int_fl0 & I2C_ERROR) {
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}
