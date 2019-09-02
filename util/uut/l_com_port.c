/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "com_port.h"
#include "main.h"

/*---------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */

#define INBUFSIZE 2048
#define OUTBUFSIZE 2048
#define LOWER_THRESHOLD 16
#define UPPER_THRESHOLD 512
#define XOFF_CHAR 0x13
#define XON_CHAR 0x11

#define UART_FIFO_SIZE 16

#define COMMAND_TIMEOUT 10000 /* 10 seconds */

/*---------------------------------------------------------------------------
 * Global variables
 *---------------------------------------------------------------------------
 */
static struct termios savetty;

/*---------------------------------------------------------------------------
 * Functions prototypes
 *---------------------------------------------------------------------------
 */

/*--------------------------------------------------------------------------
 * Local Function implementation
 *--------------------------------------------------------------------------
 */

/*--------------------------------------------------------------------------
 * Function:	convert_baudrate_to_baudrate_mask
 *
 * Parameters:
 *		baudrate - Bauderate value.
 *
 * Returns:	Baudrate mask.
 * Side effects:
 * Description:
 *		This routine convert from baudrate mode to paudrate mask.
 *--------------------------------------------------------------------------
 */
static speed_t convert_baudrate_to_baudrate_mask(uint32_t baudrate)
{
	switch (baudrate) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	default:
		return B0;
	}
}

/*-------------------------------------------------------------------------
 * Function:	set_read_blocking
 *
 * Parameters:
 *		dev_drv	- The opened handle returned by com_port_open()
 *		block		- TRUE means read in blocking mode
 *				  FALSE means read in non-blocking mode.
 *
 * Returns:	none
 * Side effects:
 * Description:
 *		This routine set/unset read blocking mode.
 *--------------------------------------------------------------------------
 */
void set_read_blocking(int dev_drv, bool block)
{
	struct termios tty;

	memset(&tty, 0, sizeof(tty));

	if (tcgetattr(dev_drv, &tty) != 0) {
		display_color_msg(FAIL,
			"set_read_blocking Error: %d Fail to get attribute "
			"from Device number %d.\n",
			errno, dev_drv);
		return;
	}

	tty.c_cc[VMIN] = block;
	tty.c_cc[VTIME] = 5; /* 0.5 seconds read timeout */

	if (tcsetattr(dev_drv, TCSANOW, &tty) != 0) {
		display_color_msg(FAIL,
			"set_read_blocking Error: %d Fail to set attribute to "
			"Device number %d.\n",
			errno, dev_drv);
	}
}

/*--------------------------------------------------------------------------
 * Global Function implementation
 *--------------------------------------------------------------------------
 */

/******************************************************************************
 * Function: int com_config_uart()
 *
 * Purpose:  Configures the Uart port properties.
 *
 * Params:   h_dev_drv - the opened handle returned by com_port_open()
 *	    com_port_fields  - a struct filled with Comport settings, see
 *			     definition above.
 *
 * Returns:  1 if successful
 *	    0 in the case of an error.
 *
 *****************************************************************************
 */
bool com_config_uart(int h_dev_drv, struct comport_fields com_port_fields)
{
	struct termios tty;
	speed_t baudrate;

	memset(&tty, 0, sizeof(tty));

	if (tcgetattr(h_dev_drv, &tty) != 0) {
		display_color_msg(FAIL,
			"com_config_uart Error: Fail to get attribute from "
			"Device number %d.\n",
			h_dev_drv);
		return false;
	}

	baudrate = convert_baudrate_to_baudrate_mask(com_port_fields.baudrate);
	cfsetospeed(&tty, baudrate);
	cfsetispeed(&tty, baudrate);

	tty.c_cflag |= baudrate;

	tty.c_cflag |= com_port_fields.byte_size;

	/*
	 * disable IGNBRK for mismatched speed tests; otherwise receive break
	 * as \000 chars
	 */

	/* Set port to be in a "raw" mode. */
	tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
			 ICRNL | IXON);
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	tty.c_oflag = ~OPOST;
	tty.c_cc[VMIN] = 0;  /* read doesn't block		*/
	tty.c_cc[VTIME] = 5; /* 0.5 seconds read timeout	*/

	tty.c_iflag |= (com_port_fields.flow_control == 0x01)
			       ? (IXON | IXOFF)
			       : 0x00; /* xon/xoff ctrl */

	tty.c_cflag |= (CLOCAL | CREAD); /* ignore modem controls */
	/* enable reading */
	tty.c_cflag &= ~(PARENB | PARODD); /* shut off parity */
	tty.c_cflag |= com_port_fields.parity;
	/* Stop bits */
	tty.c_cflag |= (com_port_fields.stop_bits == 0x02) ? CSTOPB : 0x00;
	/* HW flow control */
	tty.c_cflag |= (com_port_fields.flow_control == 0x02) ? CRTSCTS : 0x00;

	/* Flush Port, then applies attributes */
	tcflush(h_dev_drv, TCIFLUSH);

	if (tcsetattr(h_dev_drv, TCSANOW, &tty) != 0) {
		display_color_msg(FAIL,
			"com_config_uart Error: %d setting port handle %d: %s.\n",
			errno, h_dev_drv, strerror(errno));
		return false;
	}

	return true;
}

/**
 * Drain the console RX buffer before programming. The device should be in
 * programming mode and shouldn't be printing anything. Anything that's
 * currently in the buffer could interfere with programming. discard_input
 * will discard everything currently in the buffer. It prints any non zero
 * characters and returns when the console is empty and ready for programming.
 *
 * This is the same as discard_input in stm32mon.
 * TODO: create common library for initializing serial consoles.
 */
static void discard_input(int fd)
{
	uint8_t buffer[64];
	int res, i;
	int count_of_zeros;

	/* Keep track of discarded zeros */
	count_of_zeros = 0;
	do {
		res = read(fd, buffer, sizeof(buffer));
		if (res > 0) {

			/* Discard zeros in the beginning of the buffer. */
			for (i = 0; i < res; i++)
				if (buffer[i])
					break;

			count_of_zeros += i;
			if (i == res) {
				/* Only zeros, nothing to print out. */
				continue;
			}

			/* Discard zeros in the end of the buffer. */
			while (!buffer[res - 1]) {
				count_of_zeros++;
				res--;
			}

			printf("Recv[%d]:", res - i);
			for (; i < res; i++)
				printf("%02x ", buffer[i]);
			printf("\n");
		}
	} while (res > 0);

	if (count_of_zeros)
		printf("%d zeros ignored\n", count_of_zeros);
}


/******************************************************************************
 * Function: int com_port_open()
 *
 * Purpose:  Open the specified ComPort device and return its handle.
 *
 * Params:   com_port_dev_name - The name of the device to open
 *           com_port_fields - a struct filled with Comport settings
 *
 * Returns:  INVALID_HANDLE_VALUE (-1) - invalid handle.
 *           Other value - Handle to be used in other Comport APIs
 *
 * Comments: The returned handle can be used for other Win32 API communication
 *           function.
 *
 *****************************************************************************
 */
int com_port_open(const char *com_port_dev_name,
				struct comport_fields com_port_fields)
{
	int port_handler;

	port_handler = open(com_port_dev_name, O_RDWR | O_NOCTTY);

	if (port_handler < 0) {
		display_color_msg(FAIL,
				"com_port_open Error %d opening %s: %s\n",
				errno, com_port_dev_name, strerror(errno));
		return INVALID_HANDLE_VALUE;
	}

	tcgetattr(port_handler, &savetty);

	if (!com_config_uart(port_handler, com_port_fields)) {
		display_color_msg(FAIL,
			"com_port_open() Error %d, Failed on com_config_uart() %s, "
			"%s\n",
			errno, com_port_dev_name, strerror(errno));
		close(port_handler);
		return INVALID_HANDLE_VALUE;
	}

	/*
	 * Drain the console, so what ever is already in the EC console wont
	 * interfere with programming.
	 */
	discard_input(port_handler);

	return port_handler;
}

/******************************************************************************
 * Function: com_port_close()
 *
 * Purpose:  Close the ComPort device specified by Handle
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *
 * Returns:  1 if successful
 *           0 in the case of an error.
 *
 *****************************************************************************
 */
bool com_port_close(int device_id)
{
	tcsetattr(device_id, TCSANOW, &savetty);

	if (close(device_id) == INVALID_HANDLE_VALUE) {
		display_color_msg(FAIL,
			"com_port_close() Error: %d Device com%u was not opened, "
			"%s.\n",
			errno, (uint32_t)device_id, strerror(errno));
		return false;
	}

	return true;
}

/******************************************************************************
 * Function: com_port_write_bin()
 *
 * Purpose:  Send binary data through Comport
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *           buffer - contains the binary data to send
 *           buf_size - the size of data to send
 *
 * Returns:  1 if successful
 *           0 in the case of an error.
 *
 * Comments: The caller must ensure that buf_size is not bigger than
 *           buffer size.
 *
 *****************************************************************************
 */
bool com_port_write_bin(int device_id, const uint8_t *buffer,
						uint32_t buf_size)
{
	uint32_t bytes_written;

	bytes_written = write(device_id, buffer, buf_size);
	if (bytes_written != buf_size) {
		display_color_msg(FAIL,
			"com_port_write_bin() Error: %d  Failed to write data to "
			"Uart Port %d, %s.\n",
			errno, (uint32_t)device_id, strerror(errno));

		return false;
	}

	return true;
}

/******************************************************************************
 * Function: uint32_t com_port_read_bin()
 *
 * Purpose:  Read a binary data from Comport
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *           buffer - this buffer will contain the arrived data
 *           buf_size - maximum data size to read
 *
 * Returns:  The number of bytes read.
 *
 * Comments: The caller must ensure that Size is not bigger than buffer size.
 *
 *****************************************************************************
 */
uint32_t com_port_read_bin(int device_id, uint8_t *buffer, uint32_t buf_size)
{
	int32_t read_bytes;

	/* Reset read blocking mode */
	set_read_blocking(device_id, false);

	read_bytes = read(device_id, buffer, buf_size);

	if (read_bytes == -1) {
		display_color_msg(FAIL,
			"%s() Error: %d Device number %u was not "
			"opened, %s.\n",
			__func__, errno, (uint32_t)device_id, strerror(errno));
	}

	return read_bytes;
}

/******************************************************************************
 * Function: uint32_t com_port_wait_read()
 *
 * Purpose:  Wait until a byte is received for read
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *
 * Returns:  The number of bytes that are waiting in RX queue.
 *
 *****************************************************************************
 */
uint32_t com_port_wait_read(int device_id)
{
	int32_t bytes;
	int32_t ret_val;
	struct pollfd fds;

	/* Set read blocking mode */
	set_read_blocking(device_id, true);

	/* Wait up to 10 sec until byte is received for read. */
	fds.fd = device_id;
	fds.events = POLLIN;
	ret_val = poll(&fds, 1, COMMAND_TIMEOUT);
	if (ret_val < 0) {
		display_color_msg(FAIL,
			"%s() Error: %d Device number %u %s\n",
			__func__, errno, (uint32_t)device_id, strerror(errno));
		return 0;
	}

	bytes = 0;

	/* If data is ready for read. */
	if (ret_val > 0) {
		/* Get number of bytes that are ready to be read. */
		if (ioctl(device_id, FIONREAD, &bytes) < 0) {
			display_color_msg(FAIL,
				"com_port_wait_for_read() Error: %d Device number "
				"%u %s\n",
				errno, (uint32_t)device_id, strerror(errno));
			return 0;
		}
	}

	return bytes;
}
