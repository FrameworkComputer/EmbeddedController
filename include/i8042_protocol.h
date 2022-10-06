/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * i8042 keyboard protocol constants.
 *
 * See the IBM PS/2 Hardware Interface Technical Reference Manual
 *
 * https://archive.org/details/ps-2-hardware-interface-technical-reference-ocr/PS2HardwareInterfaceTechnicalReference-OCR/page/n371/mode/1up
 */

#ifndef __CROS_EC_I8042_PROTOCOL_H
#define __CROS_EC_I8042_PROTOCOL_H

/* Some commands appear more than once.  Why? */

/* port 0x64 */
#define I8042_READ_CMD_BYTE 0x20
#define I8042_READ_CTL_RAM 0x21
#define I8042_READ_CTL_RAM_END 0x3f
#define I8042_WRITE_CMD_BYTE 0x60 /* expect a byte on port 0x60 */
#define I8042_WRITE_CTL_RAM 0x61
#define I8042_WRITE_CTL_RAM_END 0x7f
#define I8042_ROUTE_AUX0 0x90
#define I8042_ROUTE_AUX1 0x91
#define I8042_ROUTE_AUX2 0x92
#define I8042_ROUTE_AUX3 0x93
#define I8042_ENA_PASSWORD 0xa6
#define I8042_DIS_MOUSE 0xa7
#define I8042_ENA_MOUSE 0xa8
#define I8042_TEST_MOUSE 0xa9
#define I8042_TEST_MOUSE_NO_ERROR 0x00
#define I8042_TEST_MOUSE_CLK_STUCK_LOW 0x01
#define I8042_TEST_MOUSE_CLK_STUCK_HIGH 0x02
#define I8042_TEST_MOUSE_DATA_STUCK_LOW 0x03
#define I8042_TEST_MOUSE_DATA_STUCK_HIGH 0x04
#define I8042_RESET_SELF_TEST 0xaa
#define I8042_TEST_KB_PORT 0xab
#define I8042_DIS_KB 0xad
#define I8042_ENA_KB 0xae
#define I8042_READ_OUTPUT_PORT 0xd0
#define I8042_WRITE_OUTPUT_PORT 0xd1
#define I8042_ECHO_MOUSE 0xd3 /* expect a byte on port 0x60 */
#define I8042_SEND_TO_MOUSE 0xd4 /* expect a byte on port 0x60 */
#define I8042_DISABLE_A20 0xdd
#define I8042_ENABLE_A20 0xdf
#define I8042_PULSE_START 0xf0
#define I8042_SYSTEM_RESET 0xfe
#define I8042_PULSE_END 0xff

/* port 0x60 return value */
#define I8042_RET_NAK 0xfe

/* port 64 - command byte bits */
#define I8042_XLATE BIT(6)
#define I8042_AUX_DIS BIT(5)
#define I8042_KBD_DIS BIT(4)
#define I8042_SYS_FLAG BIT(2)
#define I8042_ENIRQ12 BIT(1)
#define I8042_ENIRQ1 BIT(0)

/* Status Flags */
#define I8042_AUX_DATA BIT(5)

#endif /* __CROS_EC_I8042_PROTOCOL_H */
