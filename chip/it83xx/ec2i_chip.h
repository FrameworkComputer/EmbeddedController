/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EC2I control module for IT83xx. */

#ifndef __CROS_EC_IT83XX_EC2I_H
#define __CROS_EC_IT83XX_EC2I_H

/* Index list of the host interface registers of PNPCFG */
enum host_pnpcfg_index {
	/* Logical Device Number */
	HOST_INDEX_LDN = 0x07,
	/* Chip ID Byte 1 */
	HOST_INDEX_CHIPID1 = 0x20,
	/* Chip ID Byte 2 */
	HOST_INDEX_CHIPID2 = 0x21,
	/* Chip Version */
	HOST_INDEX_CHIPVER = 0x22,
	/* Super I/O Control */
	HOST_INDEX_SIOCTRL = 0x23,
	/* Super I/O IRQ Configuration */
	HOST_INDEX_SIOIRQ = 0x25,
	/* Super I/O General Purpose */
	HOST_INDEX_SIOGP = 0x26,
	/* Super I/O Power Mode */
	HOST_INDEX_SIOPWR = 0x2D,
	/* Depth 2 I/O Address */
	HOST_INDEX_D2ADR = 0x2E,
	/* Depth 2 I/O Data */
	HOST_INDEX_D2DAT = 0x2F,
	/* Logical Device Activate Register */
	HOST_INDEX_LDA = 0x30,
	/* I/O Port Base Address Bits [15:8] for Descriptor 0 */
	HOST_INDEX_IOBAD0_MSB = 0x60,
	/* I/O Port Base Address Bits [7:0] for Descriptor 0 */
	HOST_INDEX_IOBAD0_LSB = 0x61,
	/* I/O Port Base Address Bits [15:8] for Descriptor 1 */
	HOST_INDEX_IOBAD1_MSB = 0x62,
	/* I/O Port Base Address Bits [7:0] for Descriptor 1 */
	HOST_INDEX_IOBAD1_LSB = 0x63,
	/* Interrupt Request Number and Wake-Up on IRQ Enabled */
	HOST_INDEX_IRQNUMX = 0x70,
	/* Interrupt Request Type Select */
	HOST_INDEX_IRQTP = 0x71,
	/* DMA Channel Select 0 */
	HOST_INDEX_DMAS0 = 0x74,
	/* DMA Channel Select 1 */
	HOST_INDEX_DMAS1 = 0x75,
	/* Device Specific Logical Device Configuration 1 to 10 */
	HOST_INDEX_DSLDC1 = 0xF0,
	HOST_INDEX_DSLDC2 = 0xF1,
	HOST_INDEX_DSLDC3 = 0xF2,
	HOST_INDEX_DSLDC4 = 0xF3,
	HOST_INDEX_DSLDC5 = 0xF4,
	HOST_INDEX_DSLDC6 = 0xF5,
	HOST_INDEX_DSLDC7 = 0xF6,
	HOST_INDEX_DSLDC8 = 0xF7,
	HOST_INDEX_DSLDC9 = 0xF8,
	HOST_INDEX_DSLDC10 = 0xF9,
};

/* List of logical device number (LDN) assignments */
enum logical_device_number {
	/* Serial Port 1 */
	LDN_UART1 = 0x01,
	/* Serial Port 2 */
	LDN_UART2 = 0x02,
	/* System Wake-Up Control */
	LDN_SWUC = 0x04,
	/* KBC/Mouse Interface */
	LDN_KBC_MOUSE = 0x05,
	/* KBC/Keyboard Interface */
	LDN_KBC_KEYBOARD = 0x06,
	/* Consumer IR */
	LDN_CIR = 0x0A,
	/* Shared Memory/Flash Interface */
	LDN_SMFI = 0x0F,
	/* RTC-like Timer */
	LDN_RTCT = 0x10,
	/* Power Management I/F Channel 1 */
	LDN_PMC1 = 0x11,
	/* Power Management I/F Channel 2 */
	LDN_PMC2 = 0x12,
	/* Serial Peripheral Interface */
	LDN_SSPI = 0x13,
	/* Platform Environment Control Interface */
	LDN_PECI = 0x14,
	/* Power Management I/F Channel 3 */
	LDN_PMC3 = 0x17,
	/* Power Management I/F Channel 4 */
	LDN_PMC4 = 0x18,
	/* Power Management I/F Channel 5 */
	LDN_PMC5 = 0x19,
};

/* EC2I read/write message */
enum ec2i_message {
	/* EC2I write success */
	EC2I_WRITE_SUCCESS = 0x00,
	/* EC2I write error */
	EC2I_WRITE_ERROR = 0x01,
	/* EC2I read success */
	EC2I_READ_SUCCESS = 0x8000,
	/* EC2I read error */
	EC2I_READ_ERROR = 0x8100,
};

/* Data structure for initializing PNPCFG via ec2i. */
struct ec2i_t {
	/* index port */
	enum host_pnpcfg_index index_port;
	/* data port */
	uint8_t data_port;
};

extern const struct ec2i_t pnpcfg_settings[];

/* EC2I write */
enum ec2i_message ec2i_write(enum host_pnpcfg_index index, uint8_t data);

/* EC2I read */
enum ec2i_message ec2i_read(enum host_pnpcfg_index index);

#endif /* __CROS_EC_IT83XX_EC2I_H */
