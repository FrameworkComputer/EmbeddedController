/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parade Tech Type-C port controller */

#ifndef __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H
#define __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H

#include "usb_mux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C interface */
#define PS8XXX_I2C_ADDR1_P1_FLAGS 0x09
#define PS8XXX_I2C_ADDR1_P2_FLAGS 0x0A
#define PS8XXX_I2C_ADDR1_FLAGS 0x0B /* P3 */
#define PS8XXX_I2C_ADDR2_FLAGS 0x1B
#define PS8XXX_I2C_ADDR3_FLAGS 0x2B
#define PS8XXX_I2C_ADDR4_FLAGS 0x4B

#define PS8XXX_VENDOR_ID 0x1DA0

/* Minimum Delay for reset assertion */
#define PS8XXX_RESET_DELAY_MS 1

/* Delay between releasing reset and the first I2C read */
#define PS8805_FW_INIT_DELAY_MS 10

/* Delay from power on to reset de-asserted */
#define PS8815_PWR_H_RST_H_DELAY_MS 20

/*
 * Add delay of writing TCPC_REG_POWER_CTRL makes
 * CC status being judged correctly when disable VCONN.
 * This may be a PS8XXX firmware issue, Parade is still trying.
 * https://partnerissuetracker.corp.google.com/issues/185202064
 */
#define PS8XXX_VCONN_TURN_OFF_DELAY_US 10

/*
 * Delay between releasing reset and the first I2C read
 *
 * If the delay is too short, I2C fails.
 * If the delay is marginal I2C reads return garbage.
 *
 * With firmware 0x03:
 *   10ms is too short
 *   20ms is marginal
 *   25ms is OK
 */
#define PS8815_FW_INIT_DELAY_MS 50

/* NOTE: The Product ID will read as 0x8803 if the firmware has malfunctioned in
 * 8705, 8755 and 8805.
 */
#define PS8705_PRODUCT_ID 0x8705
#define PS8745_PRODUCT_ID 0x8745
#define PS8751_PRODUCT_ID 0x8751
#define PS8755_PRODUCT_ID 0x8755
#define PS8805_PRODUCT_ID 0x8805
#define PS8815_PRODUCT_ID 0x8815

extern const struct tcpm_drv ps8xxx_tcpm_drv;

/**
 * Board-specific callback to judge and provide which chip source of PS8XXX
 * series supported by this driver per specific port.
 *
 * If the board supports only one single source then there is no nencessary to
 * provide the __override version.
 *
 * If board supports two sources or above (with CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
 * then the __override version is mandatory.
 *
 * @param port	TCPC port number.
 */
__override_proto uint16_t board_get_ps8xxx_product_id(int port);

void ps8xxx_tcpc_update_hpd_status(const struct usb_mux *me,
				   mux_state_t mux_state, bool *ack_required);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
extern struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev;
#endif /* defined(CONFIG_CMD_I2C_STRESS_TEST_TCPC) */

/*
 * This driver was designed to use Low Power Mode on PS8751 TCPC/MUX chip
 * when running as MUX only (CC lines are not connected, eg. Ampton).
 * To achieve this RP on CC lines is set when device should enter LPM and
 * RD when mux should work.
 */
extern const struct usb_mux_driver ps8xxx_usb_mux_driver;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H */
