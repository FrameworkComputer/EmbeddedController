/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ITE IT5205 Type-C USB alternate mode mux.
 */

#ifndef __CROS_EC_IT5205_H
#define __CROS_EC_IT5205_H

#include "stdbool.h"
#include "usb_mux.h"

/* I2C interface */
#define IT5205_I2C_ADDR1_FLAGS 0x48
#define IT5205_I2C_ADDR2_FLAGS 0x58

/* Chip ID registers */
#define IT5205_REG_CHIP_ID3 0x4
#define IT5205_REG_CHIP_ID2 0x5
#define IT5205_REG_CHIP_ID1 0x6
#define IT5205_REG_CHIP_ID0 0x7

/* MUX power down register */
#define IT5205_REG_MUXPDR        0x10
#define IT5205_MUX_POWER_DOWN    BIT(0)

/* MUX control register */
#define IT5205_REG_MUXCR         0x11
#define IT5205_POLARITY_INVERTED BIT(4)

#define IT5205_DP_USB_CTRL_MASK  0x0f
#define IT5205_DP                0x0f
#define IT5205_DP_USB            0x03
#define IT5205_USB               0x07


/* IT5205-H SBU module */

/* I2C address for SBU switch control */
#define IT5205H_SBU_I2C_ADDR_FLAGS 0x6a

/* Vref Select Register */
#define IT5205H_REG_VSR            0x10
#define IT5205H_VREF_SELECT_MASK   0x30
#define IT5205H_VREF_SELECT_3_3V   0x00
#define IT5205H_VREF_SELECT_OFF    0x20

/* CSBU OVP Select Register */
#define IT5205H_REG_CSBUOVPSR      0x1e
#define IT5205H_OVP_SELECT_MASK    0x30
#define IT5205H_OVP_3_90V          0x00
#define IT5205H_OVP_3_68V          0x10
#define IT5205H_OVP_3_62V          0x20
#define IT5205H_OVP_3_57V          0x30

/* CSBU Switch Register */
#define IT5205H_REG_CSBUSR         0x22
#define IT5205H_CSBUSR_SWITCH      BIT(0)

/* Interrupt Switch Register */
#define IT5205H_REG_ISR            0x25
#define IT5205H_ISR_CSBU_MASK      BIT(4)
#define IT5205H_ISR_CSBU_OVP       BIT(0)

enum ec_error_list it5205h_enable_csbu_switch(const struct usb_mux *me,
					      bool en);

#endif /* __CROS_EC_IT5205_H */
