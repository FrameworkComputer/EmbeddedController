/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Parade PS8743 USB Type-C Redriving Switch for USB Host / DisplayPort.
 */

#ifndef __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H
#define __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H

#include "usb_mux.h"

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS8743_I2C_ADDR0_FLAG 0x10
#define PS8743_I2C_ADDR1_FLAG 0x11
#define PS8743_I2C_ADDR2_FLAG 0x19
#define PS8743_I2C_ADDR3_FLAG 0x1a

/* Mode register for setting mux */
#define PS8743_REG_MODE 0x00
#define PS8743_MODE_IN_HPD_ASSERT BIT(0)
#define PS8743_MODE_IN_HPD_CONTROL BIT(1)
#define PS8743_MODE_FLIP_ENABLE BIT(2)
#define PS8743_MODE_FLIP_REG_CONTROL BIT(3)
#define PS8743_MODE_USB_ENABLE BIT(4)
#define PS8743_MODE_USB_REG_CONTROL BIT(5)
#define PS8743_MODE_DP_ENABLE BIT(6)
#define PS8743_MODE_DP_REG_CONTROL BIT(7)
/* To reset the state machine to default */
#define PS8743_MODE_POWER_DOWN \
	(PS8743_MODE_USB_REG_CONTROL | PS8743_MODE_DP_REG_CONTROL)
/* DP output setting */
#define PS8743_REG_DP_SETTING 0x07
#define PS8743_DP_SWG_ADJ_DFLT 0x00
#define PS8743_DP_SWG_ADJ_N20P 0x40
#define PS8743_DP_SWG_ADJ_N15P 0x80
#define PS8743_DP_SWG_ADJ_P15P 0xc0
#define PS8743_DP_OUT_SWG_400 0x00
#define PS8743_DP_OUT_SWG_600 0x10
#define PS8743_DP_OUT_SWG_800 0x20
#define PS8743_DP_OUT_SWG_1000 0x30
#define PS8743_DP_OUT_PRE_EM_0_DB 0x00
#define PS8743_DP_OUT_PRE_EM_3_5_DB 0x04
#define PS8743_DP_OUT_PRE_EM_6_0_DB 0x08
#define PS8743_DP_OUT_PRE_EM_9_5_DB 0x0c
#define PS8743_DP_POST_CUR2_0_DB 0x00
#define PS8743_DP_POST_CUR2_NEG_0_9_DB 0x01
#define PS8743_DP_POST_CUR2_NEG_1_9_DB 0x02
#define PS8743_DP_POST_CUR2_NEG_3_1_DB 0x03

/* USB equalization settings for Host to Mux */
#define PS8743_REG_USB_EQ_TX 0x32
#define PS8743_USB_EQ_TX_12_8_DB 0x00
#define PS8743_USB_EQ_TX_17_DB 0x20
#define PS8743_USB_EQ_TX_7_7_DB 0x40
#define PS8743_USB_EQ_TX_3_6_DB 0x60
#define PS8743_USB_EQ_TX_15_DB 0x80
#define PS8743_USB_EQ_TX_10_9_DB 0xc0
#define PS8743_USB_EQ_TX_4_5_DB 0xe0

/* USB swing adjust for Mux to Type-C connector */
#define PS8743_REG_USB_SWING 0x36
#define PS8743_OUT_SWG_DEFAULT 0x00
#define PS8743_OUT_SWG_NEG_20 0x40
#define PS8743_OUT_SWG_NEG_15 0x80
#define PS8743_OUT_SWG_POS_15 0xc0
#define PS8743_LFPS_SWG_DEFAULT 0x00
#define PS8743_LFPS_SWG_TD 0x08

/* USB equalization settings for Connector to Mux */
#define PS8743_REG_USB_EQ_RX 0x3b
#define PS8743_USB_EQ_RX_2_4_DB 0x00
#define PS8743_USB_EQ_RX_5_DB 0x10
#define PS8743_USB_EQ_RX_6_5_DB 0x20
#define PS8743_USB_EQ_RX_7_4_DB 0x30
#define PS8743_USB_EQ_RX_8_7_DB 0x40
#define PS8743_USB_EQ_RX_10_9_DB 0x50
#define PS8743_USB_EQ_RX_12_8_DB 0x60
#define PS8743_USB_EQ_RX_13_8_DB 0x70
#define PS8743_USB_EQ_RX_14_8_DB 0x80
#define PS8743_USB_EQ_RX_15_4_DB 0x90
#define PS8743_USB_EQ_RX_16_0_DB 0xa0
#define PS8743_USB_EQ_RX_16_7_DB 0xb0
#define PS8743_USB_EQ_RX_18_8_DB 0xc0
#define PS8743_USB_EQ_RX_21_3_DB 0xd0
#define PS8743_USB_EQ_RX_22_2_DB 0xe0

/* USB High Speed Signal Detector thershold adjustment */
#define PS8743_REG_HS_DET_THRESHOLD 0x3c
#define PS8743_USB_HS_THRESH_DEFAULT 0x00
#define PS8743_USB_HS_THRESH_POS_10 0x20
#define PS8743_USB_HS_THRESH_POS_33 0x40
#define PS8743_USB_HS_THRESH_NEG_10 0x60
#define PS8743_USB_HS_THRESH_NEG_25 0x80
#define PS8743_USB_HS_THRESH_POS_25 0xa0
#define PS8743_USB_HS_THRESH_NEG_45 0xc0
#define PS8743_USB_HS_THRESH_NEG_35 0xe0

/* DCI config: 0x45~0x4D */
#define PS8743_REG_DCI_CONFIG_2 0x47
#define PS8743_AUTO_DCI_MODE_SHIFT 6
#define PS8743_AUTO_DCI_MODE_MASK (3 << PS8743_AUTO_DCI_MODE_SHIFT)
#define PS8743_AUTO_DCI_MODE_ENABLE (0 << PS8743_AUTO_DCI_MODE_SHIFT)
#define PS8743_AUTO_DCI_MODE_FORCE_USB (2 << PS8743_AUTO_DCI_MODE_SHIFT)
#define PS8743_AUTO_DCI_MODE_FORCE_DCI (3 << PS8743_AUTO_DCI_MODE_SHIFT)

int ps8743_tune_usb_eq(const struct usb_mux *me, uint8_t tx, uint8_t rx);
int ps8743_write(const struct usb_mux *me, uint8_t reg, uint8_t val);
int ps8743_read(const struct usb_mux *me, uint8_t reg, int *val);
int ps8743_field_update(const struct usb_mux *me, uint8_t reg, uint8_t mask,
			uint8_t val);
int ps8743_check_chip_id(const struct usb_mux *me, int *val);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_USB_MUX_PS8743_PUBLIC_H */
