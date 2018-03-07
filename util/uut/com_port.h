/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file defines the ComPort interface header file. */

#ifndef __UTIL_UUT_COM_PORT_H
#define __UTIL_UUT_COM_PORT_H

#include <stdbool.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif
/*---------------------------------------------------------------------------
 *                                      ComPort INTERFACE
 *---------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */
#define INVALID_HANDLE_VALUE -1

/*---------------------------------------------------------------------------
 * Type definitions
 *---------------------------------------------------------------------------
 */
#define COMP_PORT_PREFIX_1 "ttyS"
#define COMP_PORT_PREFIX_2 "ttyUSB"
#define COMP_PORT_PREFIX_3 "pts"

struct comport_fields {
	uint32_t baudrate;    /* Baudrate at which running               */
	tcflag_t byte_size;   /* Number of bits/byte, 4-8                */
	tcflag_t parity;       /* 0-4=None,Odd,Even,Mark,Space            */
	uint8_t stop_bits;    /* 0,1,2 = 1, 1.5, 2                       */
	uint8_t flow_control; /* 0-none, 1-SwFlowControl,2-HwFlowControl */
};

/*---------------------------------------------------------------------------
 * Function: int com_port_open()
 *
 * Purpose:  Open the specified ComPort device.
 *
 *  Params:   com_port_dev_name - The name of the device to open
 *           com_port_fields - a struct filled with Comport settings, see
 *                           definition above.
 *
 * Returns:  INVALID_HANDLE_VALUE (-1) - invalid handle.
 *           Other value - Handle to be used in other Comport APIs
 *
 * Comments: The returned handle can be used for other Win32 API communication
 *           function by casting it to HANDLE.
 *
 *---------------------------------------------------------------------------
 */
int com_port_open(const char *com_port_dev_name,
				struct comport_fields com_port_fields);

/*---------------------------------------------------------------------------
 * Function: int com_config_uart()
 *
 * Purpose:  Configures the Uart port properties.
 *
 * Params:   h_dev_drv - the opened handle returned by com_port_open()
 *           com_port_fields - a struct filled with Comport settings, see
 *                           definition above.
 *
 * Returns:  1 if successful
 *           0 in the case of an error.
 *
 *---------------------------------------------------------------------------
 */
bool com_config_uart(int h_dev_drv, struct comport_fields com_port_fields);

/*---------------------------------------------------------------------------
 * Function: bool ComPortClose()
 *
 * Purpose:  Close the ComPort device specified by Handle
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *
 * Returns:  1 if successful
 *           0 in the case of an error.
 *
 *---------------------------------------------------------------------------
 */
bool com_port_close(int device_id);

/*---------------------------------------------------------------------------
 * Function: bool com_port_write_bin()
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
 *           Buffer size.
 *
 *---------------------------------------------------------------------------
 */
bool com_port_write_bin(int device_id, const uint8_t *buffer,
						uint32_t buf_size);

/*---------------------------------------------------------------------------
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
 * Comments: The caller must ensure that Size is not bigger than Buffer size.
 *
 *---------------------------------------------------------------------------
 */
uint32_t com_port_read_bin(int device_id, uint8_t *buffer, uint32_t buf_size);

/*---------------------------------------------------------------------------
 * Function: uint32_t com_port_wait_read()
 *
 * Purpose:  Wait until a byte is received for read
 *
 * Params:   device_id - the opened handle returned by com_port_open()
 *
 * Returns:  The number of bytes that are waiting in RX queue.
 *
 *---------------------------------------------------------------------------
 */
uint32_t com_port_wait_read(int device_id);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_UUT_COM_PORT_H */
