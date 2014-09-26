/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
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
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* Transmit timeout in microseconds */
#define I2C_TX_TIMEOUT_MASTER	(10 * MSEC)

/*
 * Max data size for a version 3 request/response packet. This is
 * big enough for EC_CMD_GET_VERSION plus header info.
 */
#define I2C_MAX_HOST_PACKET_SIZE 128

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR
#if (I2C_PORT_EC == STM32_I2C1_PORT)
#define IRQ_SLAVE STM32_IRQ_I2C1
#else
#define IRQ_SLAVE STM32_IRQ_I2C2
#endif
#endif

/**
 * Wait for ISR register to contain the specified mask.
 *
 * Returns EC_SUCCESS, EC_ERROR_TIMEOUT if timed out waiting, or
 * EC_ERROR_UNKNOWN if an error bit appeared in the status register.
 */
static int wait_isr(int port, int mask)
{
	uint64_t timeout = get_time().val + I2C_TX_TIMEOUT_MASTER;

	while (get_time().val < timeout) {
		int isr = STM32_I2C_ISR(port);

		/* Check for errors */
		if (isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR |
			STM32_I2C_ISR_NACK))
			return EC_ERROR_UNKNOWN;

		/* Check for desired mask */
		if ((isr & mask) == mask)
			return EC_SUCCESS;

		/* I2C is slow, so let other things run while we wait */
		usleep(100);
	}

	return EC_ERROR_TIMEOUT;
}

static void i2c_set_freq_port(const struct i2c_port_t *p)
{
	int port = p->port;

	/* Disable port */
	STM32_I2C_CR1(port) = 0;
	STM32_I2C_CR2(port) = 0;
	/* Set clock frequency */
	switch (p->kbps) {
	case 1000:
		STM32_I2C_TIMINGR(port) = 0x50110103;
		break;
	case 400:
		STM32_I2C_TIMINGR(port) = 0x50330309;
		break;
	case 100:
		STM32_I2C_TIMINGR(port) = 0xB0420F13;
		break;
	default: /* unknown speed, defaults to 100kBps */
		CPRINTS("I2C bad speed %d kBps", p->kbps);
		STM32_I2C_TIMINGR(port) = 0xB0420F13;
	}
	/* Enable port */
	STM32_I2C_CR1(port) = STM32_I2C_CR1_PE;
}

/**
 * Initialize on the specified I2C port.
 *
 * @param p		the I2c port
 */
static void i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;

	/* Enable clocks to I2C modules if necessary */
	if (!(STM32_RCC_APB1ENR & (1 << (21 + port))))
		STM32_RCC_APB1ENR |= 1 << (21 + port);

	if (port == 0) {
#if defined(CONFIG_HOSTCMD_I2C_SLAVE_ADDR) && \
defined(CONFIG_LOW_POWER_IDLE) && \
(I2C_PORT_EC == STM32_I2C1_PORT)
		/*
		 * Use HSI (8MHz) for i2c clock. This allows smooth wakeup
		 * from STOP mode since HSI is only clock running immediately
		 * upon exit from STOP mode.
		 */
		STM32_RCC_CFGR3 &= ~0x10;
#else
		/* Use SYSCLK for i2c clock. */
		STM32_RCC_CFGR3 |= 0x10;
#endif
	}

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	/* Set up initial bus frequencies */
	i2c_set_freq_port(p);
}

/*****************************************************************************/
#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR
/* Host command slave */
/*
 * Buffer for received host command packets (including prefix byte on request,
 * and result/size on response)
 */
static uint8_t host_buffer[I2C_MAX_HOST_PACKET_SIZE + 2];
static uint8_t params_copy[I2C_MAX_HOST_PACKET_SIZE] __aligned(4);
static int host_i2c_resp_port;
static int tx_pending;
static struct host_packet i2c_packet;

static void i2c_send_response_packet(struct host_packet *pkt)
{
	int size = pkt->response_size;
	uint8_t *out = host_buffer;
	int i = 0;

	/* Ignore host command in-progress */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result and size to first two bytes. */
	*out++ = pkt->driver_result;
	*out++ = size;

	/* Transmit data when I2C tx buffer is empty until finished. */
	while ((i < size + 2) && tx_pending) {
		if (STM32_I2C_ISR(host_i2c_resp_port) & STM32_I2C_ISR_TXIS)
			STM32_I2C_TXDR(host_i2c_resp_port) = host_buffer[i++];

		/* I2C is slow, so let other things run while we wait */
		usleep(50);
	}

	/*
	 * Set the transmitter to be in 'not full' state to keep sending
	 * '0xec' in the event loop. Because of this, the master i2c
	 * doesn't need to snoop the response stream to abort transaction.
	 */
	STM32_I2C_CR1(host_i2c_resp_port) |= STM32_I2C_CR1_TXIE;
}

/* Process the command in the i2c host buffer */
static void i2c_process_command(void)
{
	char *buff = host_buffer;

	/*
	 * TODO(crosbug.com/p/29241): Combine this functionality with the
	 * i2c_process_command function in chip/stm32/i2c-stm32f.c to make one
	 * host command i2c process function which handles all protocol
	 * versions.
	 */
	i2c_packet.send_response = i2c_send_response_packet;

	i2c_packet.request = (const void *)(&buff[1]);
	i2c_packet.request_temp = params_copy;
	i2c_packet.request_max = sizeof(params_copy);
	/* Don't know the request size so pass in the entire buffer */
	i2c_packet.request_size = I2C_MAX_HOST_PACKET_SIZE;

	/*
	 * Stuff response at buff[2] to leave the first two bytes of
	 * buffer available for the result and size to send over i2c.
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

static void i2c_event_handler(int port)
{
	int i2c_isr;
	static int rx_pending, buf_idx;

	i2c_isr = STM32_I2C_ISR(port);

	/*
	 * Check for error conditions. Note, arbitration loss and bus error
	 * are the only two errors we can get as a slave allowing clock
	 * stretching and in non-SMBus mode.
	 */
	if (i2c_isr & (STM32_I2C_ISR_ARLO | STM32_I2C_ISR_BERR)) {
		rx_pending = 0;
		tx_pending = 0;

		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear error status bits */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_BERRCF |
				STM32_I2C_ICR_ARLOCF;
	}

	/* Transfer matched our slave address */
	if (i2c_isr & STM32_I2C_ISR_ADDR) {
		if (i2c_isr & STM32_I2C_ISR_DIR) {
			/* Transmitter slave */
			/* Clear transmit buffer */
			STM32_I2C_ISR(port) |= STM32_I2C_ISR_TXE;

			/* Enable txis interrupt to start response */
			STM32_I2C_CR1(port) |= STM32_I2C_CR1_TXIE;
		} else {
			/* Receiver slave */
			buf_idx = 0;
			rx_pending = 1;
		}

		/* Clear ADDR bit by writing to ADDRCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_ADDRCF;
	}

	/* Stop condition on bus */
	if (i2c_isr & STM32_I2C_ISR_STOP) {
		rx_pending = 0;
		tx_pending = 0;

		/* Make sure TXIS interrupt is disabled */
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

		/* Clear STOPF bit by writing to STOPCF bit */
		STM32_I2C_ICR(port) |= STM32_I2C_ICR_STOPCF;
	}

	/* Receiver full event */
	if (i2c_isr & STM32_I2C_ISR_RXNE)
		host_buffer[buf_idx++] = STM32_I2C_RXDR(port);

	/* Transmitter empty event */
	if (i2c_isr & STM32_I2C_ISR_TXIS) {
		if (port == I2C_PORT_EC) { /* host is waiting for PD response */
			if (rx_pending) {
				host_i2c_resp_port = port;
				/*
				 * Disable TXIS interrupt, transmission will
				 * be done by host command task.
				 */
				STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_TXIE;

				i2c_process_command();
				/* Reset host buffer after end of transfer */
				rx_pending = 0;
				tx_pending = 1;
			} else {
				STM32_I2C_TXDR(port) = 0xec;
			}
		}
	}
}
void i2c2_event_interrupt(void) { i2c_event_handler(I2C_PORT_EC); }
DECLARE_IRQ(IRQ_SLAVE, i2c2_event_interrupt, 2);
#endif

/*****************************************************************************/
/* Interface */

int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_bytes,
	     uint8_t *in, int in_bytes, int flags)
{
	int rv = EC_SUCCESS;
	int i;

#if defined(CONFIG_I2C_SCL_GATE_ADDR) && defined(CONFIG_I2C_SCL_GATE_PORT)
	if (port == CONFIG_I2C_SCL_GATE_PORT &&
	    slave_addr == CONFIG_I2C_SCL_GATE_ADDR)
		gpio_set_level(CONFIG_I2C_SCL_GATE_GPIO, 1);
#endif

	ASSERT(out || !out_bytes);
	ASSERT(in || !in_bytes);

	/* Clear status */
	STM32_I2C_ICR(port) = 0x3F38;
	STM32_I2C_CR2(port) = 0;

	if (out_bytes || !in_bytes) {
		/* Configure the write transfer */
		STM32_I2C_CR2(port) =  ((out_bytes & 0xFF) << 16)
			| slave_addr
			| (in_bytes == 0 ? STM32_I2C_CR2_AUTOEND : 0);
		/* let's go ... */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_START;

		for (i = 0; i < out_bytes; i++) {
			rv = wait_isr(port, STM32_I2C_ISR_TXIS);
			if (rv)
				goto xfer_exit;
			/* Write next data byte */
			STM32_I2C_TXDR(port) = out[i];
		}
	}
	if (in_bytes) {
		if (out_bytes) { /* wait for completion of the write */
			rv = wait_isr(port, STM32_I2C_ISR_TC);
			if (rv)
				goto xfer_exit;
		}
		/* Configure the read transfer and (re)start */
		STM32_I2C_CR2(port) = ((in_bytes & 0xFF) << 16)
				    | STM32_I2C_CR2_RD_WRN | slave_addr
				    | STM32_I2C_CR2_AUTOEND
				    | STM32_I2C_CR2_START;

		for (i = 0; i < in_bytes; i++) {
			/* Wait for receive buffer not empty */
			rv = wait_isr(port, STM32_I2C_ISR_RXNE);
			if (rv)
				goto xfer_exit;

			in[i] = STM32_I2C_RXDR(port);
		}
	}
	rv = wait_isr(port, STM32_I2C_ISR_STOP);
	if (rv)
		goto xfer_exit;

xfer_exit:
	/* clear status */
	STM32_I2C_ICR(port) = 0x3F38;
	/* On error, queue a stop condition */
	if (rv) {
		/* queue a STOP condition */
		STM32_I2C_CR2(port) |= STM32_I2C_CR2_STOP;
		/* wait for it to take effect */
		/* Wait up to 100 us for bus idle */
		for (i = 0; i < 10; i++) {
			if (!(STM32_I2C_ISR(port) & STM32_I2C_ISR_BUSY))
				break;
			udelay(10);
		}

		/*
		 * Allow bus to idle for at least one 100KHz clock = 10 us.
		 * This allows slaves on the bus to detect bus-idle before
		 * the next start condition.
		 */
		udelay(10);
		/* re-initialize the controller */
		STM32_I2C_CR2(port) = 0;
		STM32_I2C_CR1(port) &= ~STM32_I2C_CR1_PE;
		udelay(10);
		STM32_I2C_CR1(port) |= STM32_I2C_CR1_PE;
	}

#ifdef CONFIG_I2C_SCL_GATE_ADDR
	if (port == CONFIG_I2C_SCL_GATE_PORT &&
	    slave_addr == CONFIG_I2C_SCL_GATE_ADDR)
		gpio_set_level(CONFIG_I2C_SCL_GATE_GPIO, 0);
#endif

	return rv;
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
		return gpio_get_level(g);

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
		return gpio_get_level(g);

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		(i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

static void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR
	STM32_I2C_CR1(I2C_PORT_EC) |= STM32_I2C_CR1_RXIE | STM32_I2C_CR1_ERRIE
			| STM32_I2C_CR1_ADDRIE | STM32_I2C_CR1_STOPIE;
#if defined(CONFIG_LOW_POWER_IDLE) && (I2C_PORT_EC == STM32_I2C1_PORT)
	/*
	 * If using low power idle and EC port is I2C1, then set I2C1 to wake
	 * from STOP mode on address match. Note, this only works on I2C1 and
	 * only if the clock to I2C1 is HSI 8MHz.
	 */
	STM32_I2C_CR1(I2C_PORT_EC) |= STM32_I2C_CR1_WUPEN;
#endif
	STM32_I2C_OAR1(I2C_PORT_EC) = 0x8000 | CONFIG_HOSTCMD_I2C_SLAVE_ADDR;
	task_enable_irq(IRQ_SLAVE);
#endif
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_HOSTCMD_I2C_SLAVE_ADDR
/**
 * Get protocol information
 */
static int i2c_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = (1 << 3);
	r->max_request_packet_size = I2C_MAX_HOST_PACKET_SIZE;
	r->max_response_packet_size = I2C_MAX_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     i2c_get_protocol_info,
		     EC_VER_MASK(0));
#endif

