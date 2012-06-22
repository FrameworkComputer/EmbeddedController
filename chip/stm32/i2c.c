/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* 8-bit I2C slave address */
#define I2C_ADDRESS 0x3c

/* I2C bus frequency */
#define I2C_FREQ 100000 /* Hz */

/* Clock divider for I2C controller */
#define I2C_CCR (CPU_CLOCK/(2 * I2C_FREQ))

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT 10000 /* us */

#define NUM_PORTS 2
#define I2C1      STM32_I2C1_PORT
#define I2C2      STM32_I2C2_PORT


static uint16_t i2c_sr1[NUM_PORTS];
static struct mutex i2c_mutex;

/* buffer for host commands (including error code and checksum) */
static uint8_t host_buffer[EC_PARAM_SIZE + 2];

/* current position in host buffer for reception */
static int rx_index;

/* indicates if a wait loop should abort */
static volatile int abort_transaction;

static int wait_tx(int port)
{
	static timestamp_t deadline;

	deadline.val = get_time().val + I2C_TX_TIMEOUT;
	/* wait for TxE or errors (Timeout, STOP, BERR, AF) */
	while (!(STM32_I2C_SR1(port) & (1<<7)) && !abort_transaction &&
	       (get_time().val < deadline.val))
		;
	return !(STM32_I2C_SR1(port) & (1 << 7));
}

static int i2c_write_raw(int port, void *buf, int len)
{
	int i;
	uint8_t *data = buf;

	abort_transaction = 0;
	for (i = 0; i < len; i++) {
		STM32_I2C_DR(port) = data[i];
		if (wait_tx(port)) {
			CPRINTF("TX failed\n");
			break;
		}
	}

	return len;
}

static void _send_result(int slot, int result, int size)
{
	int i;
	int len = 1;
	uint8_t sum = 0;

	ASSERT(slot == 0);
	/* record the error code */
	host_buffer[0] = result;
	if (size) {
		/* compute checksum */
		for (i = 1; i <= size; i++)
			sum += host_buffer[i];
		host_buffer[size + 1] = sum;
		len = size + 2;
	}

	/* send the answer to the AP */
	i2c_write_raw(I2C2, host_buffer, len);
}

void host_send_result(int slot, int result)
{
	_send_result(slot, result, 0);
}

void host_send_response(int slot, const uint8_t *data, int size)
{
	uint8_t *out = host_get_buffer(slot);

	if (data != out)
		memcpy(out, data, size);

	_send_result(slot, EC_RES_SUCCESS, size);
}

uint8_t *host_get_buffer(int slot)
{
	ASSERT(slot == 0);
	return host_buffer + 1 /* skip room for error code */;
}

static void i2c_event_handler(int port)
{

	/* save and clear status */
	i2c_sr1[port] = STM32_I2C_SR1(port);
	STM32_I2C_SR1(port) = 0;

	/* transfer matched our slave address */
	if (i2c_sr1[port] & (1 << 1)) {
		/* cleared by reading SR1 followed by reading SR2 */
		STM32_I2C_SR1(port);
		STM32_I2C_SR2(port);
	} else if (i2c_sr1[port] & (1 << 4)) {
		abort_transaction = 1;
		/* clear STOPF bit by reading SR1 and then writing CR1 */
		STM32_I2C_SR1(port);
		STM32_I2C_CR1(port) = STM32_I2C_CR1(port);
	}

	/* RxNE event */
	if (i2c_sr1[port] & (1 << 6)) {
		if (port == I2C2) { /* AP issued write command */
			if (rx_index >= sizeof(host_buffer) - 1) {
				rx_index = 0;
				CPRINTF("I2C message too large\n");
			}
			host_buffer[rx_index++] = STM32_I2C_DR(I2C2);
		}
	}
	/* TxE event */
	if (i2c_sr1[port] & (1 << 7)) {
		if (port == I2C2) { /* AP is waiting for EC response */
			if (rx_index) {
				/* we have an available command : execute it */
				host_command_received(0, host_buffer[0]);
				/* reset host buffer after end of transfer */
				rx_index = 0;
			} else {
				/* spurious read : return dummy value */
				STM32_I2C_DR(port) = 0xec;
			}
		}
	}
}
static void i2c2_event_interrupt(void) { i2c_event_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_EV, i2c2_event_interrupt, 3);

static void i2c_error_handler(int port)
{
	i2c_sr1[port] = STM32_I2C_SR1(port);

	if (i2c_sr1[port] & 1 << 10) {
		/* ACK failed (NACK); expected when AP reads final byte.
		 * Software must clear AF bit. */
	} else {
		abort_transaction = 1;
		CPRINTF("%s: I2C_SR1(%s): 0x%04x\n",
			__func__, port, i2c_sr1[port]);
		CPRINTF("%s: I2C_SR2(%s): 0x%04x\n",
			__func__, port, STM32_I2C_SR2(port));
	}

	STM32_I2C_SR1(port) &= ~0xdf00;
}
static void i2c2_error_interrupt(void) { i2c_error_handler(I2C2); }
DECLARE_IRQ(STM32_IRQ_I2C2_ER, i2c2_error_interrupt, 2);

static int i2c_init2(void)
{
	/* enable I2C2 clock */
	STM32_RCC_APB1ENR |= 1 << 22;

	/* force reset if the bus is stuck in BUSY state */
	if (STM32_I2C_SR2(I2C2) & 0x2) {
		STM32_I2C_CR1(I2C2) = 0x8000;
		STM32_I2C_CR1(I2C2) = 0x0000;
	}

	/* set clock configuration : standard mode (100kHz) */
	STM32_I2C_CCR(I2C2) = I2C_CCR;

	/* set slave address */
	STM32_I2C_OAR1(I2C2) = I2C_ADDRESS;

	/* configuration : I2C mode / Periphal enabled, ACK enabled */
	STM32_I2C_CR1(I2C2) = (1 << 10) | (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32_I2C_CR2(I2C2) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32_I2C_SR1(I2C2) = 0;

	/* enable event and error interrupts */
	task_enable_irq(STM32_IRQ_I2C2_EV);
	task_enable_irq(STM32_IRQ_I2C2_ER);

	CPUTS("done\n");
	return EC_SUCCESS;
}

static int i2c_init1(void)
{
	/* enable clock */
	STM32_RCC_APB1ENR |= 1 << 21;

	/* force reset if the bus is stuck in BUSY state */
	if (STM32_I2C_SR2(I2C1) & 0x2) {
		STM32_I2C_CR1(I2C1) = 0x8000;
		STM32_I2C_CR1(I2C1) = 0x0000;
	}

	/* set clock configuration : standard mode (100kHz) */
	STM32_I2C_CCR(I2C1) = I2C_CCR;

	/* configuration : I2C mode / Periphal enabled */
	STM32_I2C_CR1(I2C1) = (1 << 0);
	/* error and event interrupts enabled / input clock is 16Mhz */
	STM32_I2C_CR2(I2C1) = (1 << 9) | (1 << 8) | 0x10;

	/* clear status */
	STM32_I2C_SR1(I2C1) = 0;

	/* enable event and error interrupts */
	task_enable_irq(STM32_IRQ_I2C1_EV);
	task_enable_irq(STM32_IRQ_I2C1_ER);

	return EC_SUCCESS;

}

static int i2c_init(void)
{
	int rc = 0;

	/* FIXME: Add #defines to determine which channels to init */
	rc |= i2c_init2();
	rc |= i2c_init1();

	return rc;
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);


/*****************************************************************************/
/* STM32 Host I2C */

#define SR1_SB		(1 << 0)	/* Start bit sent */
#define SR1_ADDR	(1 << 1)	/* Address sent */
#define SR1_BTF		(1 << 2)	/* Byte transfered */
#define SR1_ADD10	(1 << 3)	/* 10bit address sent */
#define SR1_STOPF	(1 << 4)	/* Stop detected */
#define SR1_RxNE	(1 << 6)	/* Data reg not empty */
#define SR1_TxE		(1 << 7)	/* Data reg empty */
#define SR1_BERR	(1 << 8)	/* Buss error */
#define SR1_ARLO	(1 << 9)	/* Arbitration lost */
#define SR1_AF		(1 << 10)	/* Ack failure */
#define SR1_OVR		(1 << 11)	/* Overrun/underrun */
#define SR1_PECERR	(1 << 12)	/* PEC err in reception */
#define SR1_TIMEOUT	(1 << 14)	/* Timeout : 25ms */

static inline void disable_i2c_interrupt(int port)
{
	STM32_I2C_CR2(port) &= ~(3 << 8);
}

static inline void enable_i2c_interrupt(int port)
{
	STM32_I2C_CR2(port) |= 3 << 8;
}

static inline void enable_ack(int port)
{
	STM32_I2C_CR1(port) |= (1 << 10);
}

static inline void disable_ack(int port)
{
	STM32_I2C_CR1(port) &= ~(1 << 10);
}

static inline void dump_i2c_reg(int port)
{
#ifdef CONFIG_DEBUG_I2C
	CPRINTF("CR1  : %016b\n", STM32_I2C_CR1(port));
	CPRINTF("CR2  : %016b\n", STM32_I2C_CR2(port));
	CPRINTF("SR2  : %016b\n", STM32_I2C_SR2(port));
	CPRINTF("SR1  : %016b\n", STM32_I2C_SR1(port));
	CPRINTF("OAR1 : %016b\n", STM32_I2C_OAR1(port));
	CPRINTF("OAR2 : %016b\n", STM32_I2C_OAR2(port));
	CPRINTF("DR   : %016b\n", STM32_I2C_DR(port));
	CPRINTF("CCR  : %016b\n", STM32_I2C_CCR(port));
	CPRINTF("TRISE: %016b\n", STM32_I2C_TRISE(port));
#endif /* CONFIG_DEBUG_I2C */
}

static int wait_status(int port, uint32_t mask)
{
	uint32_t r;
	timestamp_t t1, t2;


	t1 = get_time();
	r = STM32_I2C_SR1(port);
	while (mask ? ((r & mask) != mask) : r) {
		t2 = get_time();
		if (t2.val - t1.val > 100000) {
#ifdef CONFIG_DEBUG_I2C
			CPRINTF(" m %016b\n", mask);
			CPRINTF(" - %016b\n", r);
#endif /* CONFIG_DEBUG_I2C */
			return EC_ERROR_TIMEOUT;
		}
		usleep(2000);
		r = STM32_I2C_SR1(port);
	}

	return EC_SUCCESS;
}

static inline uint32_t read_clear_status(int port)
{
	uint32_t sr1, sr2;

	sr1 = STM32_I2C_SR1(port);
	sr2 = STM32_I2C_SR2(port);
	return (sr2 << 16) | (sr1 & 0xffff);
}

static int master_start(int port, int slave_addr)
{
	int rv;

	/* Change to master send mode, send start bit */
	STM32_I2C_CR1(port) |= (1 << 8);
	/* Wait for start bit sent event */
	rv = wait_status(port, SR1_SB);
	if (rv)
		return rv;
	/* Send address */
	STM32_I2C_DR(port) = slave_addr;
	/* Wait for addr ready */
	rv = wait_status(port, SR1_ADDR);
	if (rv)
		return rv;
	read_clear_status(port);

	return EC_SUCCESS;
}

static void master_stop(int port)
{
	STM32_I2C_CR1(port) |= (1 << 9);
}

static void handle_i2c_error(int port, int rv)
{
	timestamp_t t1, t2;
	uint32_t r;

	/* we have not used the bus, just exit */
	if (rv == EC_ERROR_BUSY)
		return;

	if (rv)
		dump_i2c_reg(port);

	/* Clear rc_w0 bits */
	STM32_I2C_SR1(port) = 0;
	/* Clear seq read status bits */
	r = STM32_I2C_SR1(port);
	r = STM32_I2C_SR2(port);
	/* Clear busy state */
	t1 = get_time();
	while (r & 2) {
		t2 = get_time();
		if (t2.val - t1.val > 1000000) {
			dump_i2c_reg(port);
			return;
		}
		/* Send stop */
		master_stop(port);
		usleep(10000);
		r = STM32_I2C_SR2(port);
	}
}

static int i2c_master_transmit(int port, int slave_addr, uint8_t *data,
	int size, int stop)
{
	int rv, i;

	/* if the AP is ON, don't play with the connection */
	if ((port == I2C_PORT_SLAVE) && chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_BUSY;

	disable_ack(port);
	rv = master_start(port, slave_addr);
	if (rv)
		return rv;

	/* TODO: use common i2c_write_raw instead */
	for (i = 0; i < size; i++) {
		rv = wait_status(port, SR1_TxE);
		if (rv)
			return rv;
		STM32_I2C_DR(port) = data[i];
	}
	rv = wait_status(port, SR1_BTF | SR1_TxE);
	if (rv)
		return rv;

	if (stop) {
		master_stop(port);
		return wait_status(port, 0);
	}

	return EC_SUCCESS;
}

static int i2c_master_receive(int port, int slave_addr, uint8_t *data,
	int size)
{
	/* Master receiver sequence
	 *
	 * 1 byte
	 *   S   ADDR   ACK   D0   NACK   P
	 *  -o- -oooo- -iii- -ii- -oooo- -o-
	 *
	 * multi bytes
	 *   S   ADDR   ACK   D0   ACK   Dn-2   ACK   Dn-1   NACK   P
	 *  -o- -oooo- -iii- -ii- -ooo- -iiii- -ooo- -iiii- -oooo- -o-
	 *
	 */
	int rv, i;

	/* if the AP is ON, don't play with the connection */
	if ((port == I2C_PORT_SLAVE) && chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_BUSY;

	if (data == NULL || size < 1)
		return EC_ERROR_INVAL;

	/* Set ACK to high only on receiving more than 1 byte */
	if (size > 1)
		enable_ack(port);
	else
		disable_ack(port);

	/* Send START pulse, slave address, receive mode */
	rv = master_start(port, slave_addr | 1);
	if (rv)
		return rv;

	if (size >= 2) {
		for (i = 0; i < (size - 2); i++) {
			rv = wait_status(port, SR1_RxNE);
			if (rv)
				return rv;

			data[i] = STM32_I2C_DR(port);
		}

		/* Process last two bytes: data[n-2], data[n-1]
		 *   => wait rx ready
		 *   => [n-2] in data-reg, [n-1] in shift-reg
		 *   => [n-2] ACK done
		 *   => disable ACK to let [n-1] byte send NACK
		 *   => set STOP high
		 *   => read [n-2]
		 *   => wait rx ready
		 *   => read [n-1]
		 */
		rv = wait_status(port, SR1_RxNE);
		if (rv)
			return rv;

		disable_ack(port);
		master_stop(port);

		data[i] = STM32_I2C_DR(port);

		rv = wait_status(port, SR1_RxNE);
		if (rv)
			return rv;

		i++;
		data[i] = STM32_I2C_DR(port);
	} else {
		master_stop(port);
		rv = wait_status(port, SR1_RxNE);
		if (rv)
			return rv;
		data[0] = STM32_I2C_DR(port);
	}

	return EC_SUCCESS;
}

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	int rv;
	uint8_t reg, buf[2] = {0, 0};

	reg = offset & 0xff;

	mutex_lock(&i2c_mutex);
	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, &reg, 1, 0);
	if (!rv)
		rv = i2c_master_receive(port, slave_addr, buf, 2);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);
	mutex_unlock(&i2c_mutex);

	if (rv)
		return rv;

	*data = ((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write16(int port, int slave_addr, int offset, int data)
{
	int rv;
	uint8_t buf[3];

	buf[0] = offset & 0xff;
	buf[1] = data & 0xff;
	buf[2] = (data >> 8) & 0xff;

	mutex_lock(&i2c_mutex);
	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, buf, 3, 1);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);
	mutex_unlock(&i2c_mutex);

	return rv;
}

int i2c_read8(int port, int slave_addr, int offset, int *data)
{
	int rv;
	uint8_t reg, buf;

	reg = offset & 0xff;
	mutex_lock(&i2c_mutex);
	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, &reg, 1, 0);
	if (!rv)
		rv = i2c_master_receive(port, slave_addr, &buf, 1);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);
	mutex_unlock(&i2c_mutex);

	if (rv)
		return rv;

	*data = buf;

	return EC_SUCCESS;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	int rv;
	uint8_t buf[2];

	buf[0] = offset & 0xff;
	buf[1] = data & 0xff;

	mutex_lock(&i2c_mutex);
	disable_i2c_interrupt(port);

	rv = i2c_master_transmit(port, slave_addr, buf, 2, 1);
	handle_i2c_error(port, rv);

	enable_i2c_interrupt(port);
	mutex_unlock(&i2c_mutex);

	return rv;
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
	int len)
{
	/* TODO: implement i2c_read_block and i2c_read_string */

	if (len && data)
		*data = 0;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

#ifdef I2C_PORT_HOST

static int command_i2c(int argc, char **argv)
{
	int rw = 0;
	int slave_addr, offset;
	int value = 0;
	char *e;
	int rv = 0;

	if (argc < 4) {
		ccputs("Usage: i2c r/r16/w/w16 slave_addr offset [value]\n");
		return EC_ERROR_UNKNOWN;
	}

	if (strcasecmp(argv[1], "r") == 0) {
		rw = 0;
	} else if (strcasecmp(argv[1], "r16") == 0) {
		rw = 1;
	} else if (strcasecmp(argv[1], "w") == 0) {
		rw = 2;
	} else if (strcasecmp(argv[1], "w16") == 0) {
		rw = 3;
	} else {
		ccputs("Invalid rw mode : r / w / r16 / w16\n");
		return EC_ERROR_INVAL;
	}

	slave_addr = strtoi(argv[2], &e, 0);
	if (*e) {
		ccputs("Invalid slave_addr\n");
		return EC_ERROR_INVAL;
	}

	offset = strtoi(argv[3], &e, 0);
	if (*e) {
		ccputs("Invalid addr\n");
		return EC_ERROR_INVAL;
	}

	if (rw > 1) {
		if (argc < 5) {
			ccputs("No write value\n");
			return EC_ERROR_INVAL;
		}
		value = strtoi(argv[4], &e, 0);
		if (*e) {
			ccputs("Invalid write value\n");
			return EC_ERROR_INVAL;
		}
	}


	switch (rw) {
	case 0:
		rv = i2c_read8(I2C_PORT_HOST, slave_addr, offset, &value);
		break;
	case 1:
		rv = i2c_read16(I2C_PORT_HOST, slave_addr, offset, &value);
		break;
	case 2:
		rv = i2c_write8(I2C_PORT_HOST, slave_addr, offset, value);
		break;
	case 3:
		rv = i2c_write16(I2C_PORT_HOST, slave_addr, offset, value);
		break;
	}


	if (rv) {
		ccprintf("i2c command failed\n", rv);
		return rv;
	}

	if (rw == 0)
		ccprintf("0x%02x [%d]\n", value);
	else if (rw == 1)
		ccprintf("0x%04x [%d]\n", value);

	ccputs("ok\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2c, command_i2c,
			"r/r16/w/w16 slave_addr offset [value]",
			"Read write i2c",
			NULL);

#endif /* I2C_PORT_HOST */
