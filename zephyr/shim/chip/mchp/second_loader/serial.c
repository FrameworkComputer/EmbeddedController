/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "MCHP_MEC172x.h"
#include "common.h"
#include "gpio.h"
#include "serial.h"

/* HOST to EC interface */
#define HOST_IF_UART_BASE (UART0_INST)

#define LENGTH_8_BIT 0x03
#define ONE_STOP_BIT 0x00
#define BAUD_RATE_57600 2

/* 1StartBit, 8Bit, 1StopBit = 10bits = 1character;
 * Baudrate 57600, so 57600/10 = 5760bytes/sec;
 * 1/5760 = ~0.000173;
 * So shall wait for ~1ms, which is >5character rxv timing;
 *
 * With 48MHz clock, to get 1ms wait time 3000 count required.
 * Observed 1.32ms with 3000 count
 */
#define RCV_TIME_OUT_COUNT 3000

void serial_init(void)
{
	/* GPIO_MUX_FUNC1 TXD(UART0_TX) */
	gpio_pin_ctrl1_reg_write(0104, 0x001000UL);
	/* GPIO_MUX_FUNC1 RXD(UART0_RX) */
	gpio_pin_ctrl1_reg_write(0105, 0x001000UL);

	/* Init the host i/f UART0 block */
	HOST_IF_UART_BASE->FIFO_CR_b.CLEAR_RECV_FIFO = 1;
	HOST_IF_UART_BASE->FIFO_CR_b.CLEAR_XMIT_FIFO = 1;
	HOST_IF_UART_BASE->FIFO_CR_b.RECV_FIFO_TRIGGER_LEVEL = 0;
	HOST_IF_UART_BASE->FIFO_CR_b.EXRF = 1;

	/* RST by VCC1_RESET */
	HOST_IF_UART_BASE->CONFIG = 0;
	HOST_IF_UART_BASE->LINE_CR_b.DLAB = 1;
	HOST_IF_UART_BASE->BAUDRATE_LSB = BAUD_RATE_57600;
	HOST_IF_UART_BASE->BAUDRATE_MSB = 0;
	HOST_IF_UART_BASE->LINE_CR_b.DLAB = 0;
	HOST_IF_UART_BASE->LINE_CR_b.STOP_BITS = ONE_STOP_BIT;
	HOST_IF_UART_BASE->LINE_CR_b.WORD_LENGTH = LENGTH_8_BIT;
	/* MCR_OUT2 */
	HOST_IF_UART_BASE->MODEM_CR = 0x08;
	HOST_IF_UART_BASE->ACTIVATE = 1;
}

void serial_send_host_char(uint8_t data)
{
	while (HOST_IF_UART_BASE->LINE_STS_b.TRANSMIT_EMPTY == 0)
		;
	HOST_IF_UART_BASE->TX_DATA = data;
	while (HOST_IF_UART_BASE->LINE_STS_b.TRANSMIT_EMPTY)
		;
}

bool serial_receive_host_char(uint8_t *rx_data)
{
	if (HOST_IF_UART_BASE->LINE_STS & UART0_STS_DATA_RDY_Msk) {
		*rx_data = HOST_IF_UART_BASE->RX_DATA;
		return true;
	}
	return false;
}

enum failure_resp_type serial_receive_host_bytes(uint8_t *buff, uint8_t len)
{
	uint8_t i;
	uint8_t rx_data;
	uint32_t cnt;
	enum failure_resp_type ret = SERIAL_RECV_TIMEOUT;

	for (i = 0; i < len; i++) {
		cnt = RCV_TIME_OUT_COUNT;
		while (cnt--) {
			if (serial_receive_host_char(&rx_data)) {
				buff[i] = rx_data;
				ret = NO_FAILURE;
				break;
			}
		}
		if (!cnt) {
			break;
		}
	}
	return ret;
}
