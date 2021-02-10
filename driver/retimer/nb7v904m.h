/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ON Semiconductor NB7V904M USB Type-C DisplayPort Alt Mode Redriver
 */

#ifndef __CROS_EC_USB_REDRIVER_NB7V904M_H
#define __CROS_EC_USB_REDRIVER_NB7V904M_H

#include "compile_time_macros.h"
#include "usb_mux.h"

#define NB7V904M_I2C_ADDR0 0x19
#define NB7V904M_I2C_ADDR1 0x1A
#define NB7V904M_I2C_ADDR2 0x1C

/* Registers */
#define NB7V904M_REG_GEN_DEV_SETTINGS	0x00
#define NB7V904M_REG_CH_A_EQ_SETTINGS	0x01
#define NB7V904M_REG_CH_B_EQ_SETTINGS	0x03
#define NB7V904M_REG_CH_C_EQ_SETTINGS	0x05
#define NB7V904M_REG_CH_D_EQ_SETTINGS	0x07
#define NB7V904M_REG_AUX_CH_CTRL        0x09
#define NB7V904M_REG_CH_A_FLAT_GAIN		0x18
#define NB7V904M_REG_CH_A_LOSS_CTRL		0x19
#define NB7V904M_REG_CH_B_FLAT_GAIN		0x1a
#define NB7V904M_REG_CH_B_LOSS_CTRL		0x1b
#define NB7V904M_REG_CH_C_FLAT_GAIN		0x1c
#define NB7V904M_REG_CH_C_LOSS_CTRL		0x1d
#define NB7V904M_REG_CH_D_FLAT_GAIN		0x1e
#define NB7V904M_REG_CH_D_LOSS_CTRL		0x1f

/* 0x00 - General Device Settings */
#define NB7V904M_CHIP_EN        BIT(0)
#define NB7V904M_USB_DP_NORMAL  BIT(1)
#define NB7V904M_USB_DP_FLIPPED 0
#define NB7V904M_DP_ONLY        BIT(2)
#define NB7V904M_USB_ONLY       (BIT(3) | BIT(1))
#define NB7V904M_OP_MODE_MASK   GENMASK(3, 1)
#define NB7V904M_CH_A_EN        BIT(4)
#define NB7V904M_CH_B_EN        BIT(5)
#define NB7V904M_CH_C_EN        BIT(6)
#define NB7V904M_CH_D_EN        BIT(7)
#define NB7V904M_CH_EN_MASK     GENMASK(7, 4)

/* 0x01 - Channel A Equalization Settings */
#define NB7V904M_CH_A_EQ_0_DB	0x0a
#define NB7V904M_CH_A_EQ_2_DB	0x08
#define NB7V904M_CH_A_EQ_4_DB	0x0e
#define NB7V904M_CH_A_EQ_6_DB	0x0c
#define NB7V904M_CH_A_EQ_8_DB	0x02
#define NB7V904M_CH_A_EQ_10_DB	0x00

/* 0x03 - Channel B Equalization Settings */
#define NB7V904M_CH_B_EQ_0_DB	0x0e
#define NB7V904M_CH_B_EQ_2_DB	0x0c
#define NB7V904M_CH_B_EQ_4_DB	0x0a
#define NB7V904M_CH_B_EQ_6_DB	0x08
#define NB7V904M_CH_B_EQ_8_DB	0x06
#define NB7V904M_CH_B_EQ_10_DB	0x00

/* 0x05 - Channel C Equalization Settings */
#define NB7V904M_CH_C_EQ_0_DB	0x0e
#define NB7V904M_CH_C_EQ_2_DB	0x0c
#define NB7V904M_CH_C_EQ_4_DB	0x0a
#define NB7V904M_CH_C_EQ_6_DB	0x08
#define NB7V904M_CH_C_EQ_8_DB	0x06
#define NB7V904M_CH_C_EQ_10_DB	0x00

/* 0x07 - Channel D Equalization Settings */
#define NB7V904M_CH_D_EQ_0_DB	0x0a
#define NB7V904M_CH_D_EQ_2_DB	0x08
#define NB7V904M_CH_D_EQ_4_DB	0x0e
#define NB7V904M_CH_D_EQ_6_DB	0x0c
#define NB7V904M_CH_D_EQ_8_DB	0x02
#define NB7V904M_CH_D_EQ_10_DB	0x00

/* 0x09 - Auxiliary Channel Control */
#define NB7V904M_AUX_CH_NORMAL   0
#define NB7V904M_AUX_CH_FLIPPED  BIT(0)
#define NB7V904M_AUX_CH_HI_Z     BIT(1)

/* 0x18 - Channel A Flag Gain */
#define NB7V904M_CH_A_GAIN_0_DB		0x00
#define NB7V904M_CH_A_GAIN_1P5_DB	0x02
#define NB7V904M_CH_A_GAIN_3P5_DB	0x03

/* 0x1a - Channel B Flag Gain */
#define NB7V904M_CH_B_GAIN_0_DB		0x03
#define NB7V904M_CH_B_GAIN_1P5_DB	0x01
#define NB7V904M_CH_B_GAIN_3P5_DB	0x00

/* 0x1c - Channel C Flag Gain */
#define NB7V904M_CH_C_GAIN_0_DB		0x03
#define NB7V904M_CH_C_GAIN_1P5_DB	0x01
#define NB7V904M_CH_C_GAIN_3P5_DB	0x00

/* 0x1e - Channel D Flag Gain */
#define NB7V904M_CH_D_GAIN_0_DB		0x00
#define NB7V904M_CH_D_GAIN_1P5_DB	0x02
#define NB7V904M_CH_D_GAIN_3P5_DB	0x03

/* 0x19 - Channel A Loss Profile Matching Control */
/* 0x1b - Channel B Loss Profile Matching Control */
/* 0x1d - Channel C Loss Profile Matching Control */
/* 0x1f - Channel D Loss Profile Matching Control */
#define NB7V904M_LOSS_PROFILE_A		0x00
#define NB7V904M_LOSS_PROFILE_B		0x01
#define NB7V904M_LOSS_PROFILE_C		0x02
#define NB7V904M_LOSS_PROFILE_D		0x03

extern const struct usb_mux_driver nb7v904m_usb_redriver_drv;
#ifdef CONFIG_NB7V904M_LPM_OVERRIDE
extern int nb7v904m_lpm_disable;
#endif

/* Use this value if tuning eq wants to be skipped  */
#define NB7V904M_CH_ALL_SKIP_EQ	0xff
int nb7v904m_tune_usb_set_eq(const struct usb_mux *me, uint8_t eq_a,
			uint8_t eq_b, uint8_t eq_c, uint8_t eq_d);
/* Use this value if tuning gain wants to be skipped  */
#define NB7V904M_CH_ALL_SKIP_GAIN	0xff
int nb7v904m_tune_usb_flat_gain(const struct usb_mux *me, uint8_t gain_a,
			uint8_t gain_b, uint8_t gain_c, uint8_t gain_d);
/* Use this value if loss profile control  wants to be skipped  */
#define NB7V904M_CH_ALL_SKIP_LOSS	0xff
/* Control channel Loss Profile Matching */
int nb7v904m_set_loss_profile_match(const struct usb_mux *me, uint8_t loss_a,
			uint8_t loss_b, uint8_t loss_c, uint8_t loss_d);
/* Control mapping between AUX and SBU */
int nb7v904m_set_aux_ch_switch(const struct usb_mux *me, uint8_t aux_ch);
#endif /* __CROS_EC_USB_REDRIVER_NB7V904M_H */
