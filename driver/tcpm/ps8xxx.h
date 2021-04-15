/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_mux.h"

#include "driver/tcpm/ps8xxx_public.h"

/* Parade Tech Type-C port controller */

#ifndef __CROS_EC_USB_PD_TCPM_PS8XXX_H
#define __CROS_EC_USB_PD_TCPM_PS8XXX_H

#define PS8751_P3_TO_P0_FLAGS(p3_flags)	((p3_flags) - 3)
#define PS8751_P3_TO_P1_FLAGS(p3_flags)	((p3_flags) - 2)

#define PS8751_BIST_TIMER_FREQ  15000000
#define PS8751_BIST_DELAY_MS    50

#define PS8751_BIST_COUNTER (PS8751_BIST_TIMER_FREQ / MSEC \
				* PS8751_BIST_DELAY_MS)

#define PS8751_BIST_COUNTER_BYTE0 (PS8751_BIST_COUNTER & 0xff)
#define PS8751_BIST_COUNTER_BYTE1 ((PS8751_BIST_COUNTER >> 8) & 0xff)
#define PS8751_BIST_COUNTER_BYTE2 ((PS8751_BIST_COUNTER >> 16) & 0xff)

#define PS8XXX_REG_RP_DETECT_CONTROL            0x9B
#define RP_DETECT_DISABLE			0x30

#define PS8XXX_REG_I2C_DEBUGGING_ENABLE         0xA0
#define PS8XXX_REG_I2C_DEBUGGING_ENABLE_ON      0x30
#define PS8XXX_REG_I2C_DEBUGGING_ENABLE_OFF     0x31    /* default */
#define PS8XXX_REG_BIST_CONT_MODE_BYTE0         0xBC
#define PS8XXX_REG_BIST_CONT_MODE_BYTE1         0xBD
#define PS8XXX_REG_BIST_CONT_MODE_BYTE2         0xBE
#define PS8XXX_REG_BIST_CONT_MODE_CTR           0xBF
#define PS8XXX_REG_DET_CTRL0                    0x08

#define PS8XXX_REG_MUX_USB_DCI_CFG_MODE_MASK    0xC0
#define PS8XXX_REG_MUX_USB_DCI_CFG_MODE_OFF     0x80

#define MUX_IN_HPD_ASSERTION_REG                0xD0
#define IN_HPD  BIT(0)
#define HPD_IRQ BIT(1)

#define PS8XXX_P1_REG_MUX_USB_DCI_CFG           0x4B

#define PS8755_P0_REG_SM			0x06
#define PS8755_P0_REG_SM_VALUE			0x80

#if defined(CONFIG_USB_PD_TCPM_PS8751)
/* Vendor defined registers */
#define PS8XXX_REG_VENDOR_ID_L                  0x00
#define PS8XXX_REG_VENDOR_ID_H                  0x01
#define PS8XXX_REG_MUX_DP_EQ_CONFIGURATION      0xD3
#define PS8XXX_REG_MUX_DP_OUTPUT_CONFIGURATION  0xD4
#define PS8XXX_REG_MUX_USB_C2SS_EQ              0xE7
#define PS8XXX_REG_MUX_USB_C2SS_HS_THRESHOLD    0xE8
#define PS8751_REG_MUX_USB_DCI_CFG              0xED
#endif

/* Vendor defined registers */
#define PS8815_P1_REG_HW_REVISION		0xF0

/* Vendor defined registers */
#define PS8815_REG_APTX_EQ_AT_10G		0x20
#define PS8815_REG_RX_EQ_AT_10G			0x22
#define PS8815_REG_APTX_EQ_AT_5G		0x24
#define PS8815_REG_RX_EQ_AT_5G			0x26

#define PS8815_P1_REG_RESERVED_D1		0xD1
#define PS8815_P1_REG_RESERVED_D1_FRS_EN	BIT(7)
#define PS8815_P1_REG_RESERVED_F4		0xF4
#define PS8815_P1_REG_RESERVED_F4_FRS_EN	BIT(6)

/*
 * Below register is defined from Parade PS8815 Register Table,
 * See b:189587527 for more detail.
 */

/* Displayport related settings */
#define PS8815_REG_DP_EQ_SETTING		0xF8
#define PS8815_AUTO_EQ_DISABLE			BIT(7)
#define PS8815_DPEQ_LOSS_UP_21DB		0x09
#define PS8815_DPEQ_LOSS_UP_20DB		0x08
#define PS8815_DPEQ_LOSS_UP_19DB		0x07
#define PS8815_DPEQ_LOSS_UP_18DB		0x06
#define PS8815_DPEQ_LOSS_UP_17DB		0x05
#define PS8815_DPEQ_LOSS_UP_16DB		0x04
#define PS8815_DPEQ_LOSS_UP_13DB		0x03
#define PS8815_DPEQ_LOSS_UP_12DB		0x02
#define PS8815_DPEQ_LOSS_UP_10DB		0x01
#define PS8815_DPEQ_LOSS_UP_9DB			0x00
#define PS8815_REG_DP_EQ_COMP_SHIFT		3
#define PS8815_AUX_INTERCEPTION_DISABLE		BIT(1)

/*
 * PS8805 register to distinguish chip revision
 * bit 7-4: 1010b is A3 chip, 0000b is A2 chip
 */
#define PS8805_P0_REG_CHIP_REVISION		0x62

/*
 * PS8805 GPIO control register. Note the device I2C address of 0x1A is
 * independent of the ADDR pin on the chip, and not the same address being used
 * for TCPCI functions.
 */
#define PS8805_VENDOR_DEFINED_I2C_ADDR		0x1A
#define PS8805_REG_GPIO_CONTROL		0x21
#define PS8805_REG_GPIO_0			BIT(7)
#define PS8805_REG_GPIO_1			BIT(5)
#define PS8805_REG_GPIO_2			BIT(6)

enum ps8805_gpio {
	PS8805_GPIO_0,
	PS8805_GPIO_1,
	PS8805_GPIO_2,
	PS8805_GPIO_NUM,
};

/**
 * Set PS8805 gpio signal to desired level
 *
 * @param port: The Type-C port number.
 * @param signal PS8805 gpio number (0, 1, or 2)
 * @param level desired level
 * @return EC_SUCCESS if I2C accesses are successful
 */
int ps8805_gpio_set_level(int port, enum ps8805_gpio signal, int level);

/**
 * Get PS8805 gpio signal value
 *
 * @param port: The Type-C port number.
 * @param signal PS8805 gpio number (0, 1, or 2)
 * @param pointer location to store gpio level
 * @return EC_SUCCESS if I2C accesses are successful
 */
int ps8805_gpio_get_level(int port, enum ps8805_gpio signal, int *level);

/**
 * Check if the chip is PS8755
 *
 * @param port: The Type-C port number.
 * @return true if hidden register sm is 0x80
 */
bool check_ps8755_chip(int port);

/*
 * Allow boards to customize for PS8XXX initial if board has
 * specific settings.
 *
 * @param port: The Type-C port number.
 */
__override_proto void board_ps8xxx_tcpc_init(int port);

#endif /* defined(__CROS_EC_USB_PD_TCPM_PS8XXX_H) */
