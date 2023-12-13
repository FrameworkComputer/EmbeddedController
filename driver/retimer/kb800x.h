/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Kandou KB8001 USB-C 40 Gb/s multiprotocol switch.
 */

#ifndef __CROS_EC_KB800X_H
#define __CROS_EC_KB800X_H

#include "compile_time_macros.h"
#include "gpio_signal.h"
#include "usb_mux.h"

#define KB800X_I2C_ADDR0_FLAGS 0x08
#define KB800X_I2C_ADDR1_FLAGS 0x0C

extern const struct usb_mux_driver kb800x_usb_mux_driver;

/* Set the protocol */
#define KB800X_REG_PROTOCOL 0x0001
#define KB800X_PROTOCOL_USB3 0x0
#define KB800X_PROTOCOL_DPMF 0x1
#define KB800X_PROTOCOL_DP 0x2
#define KB800X_PROTOCOL_CIO 0x3

/* Configure the lane orientaitons */
#define KB800X_REG_ORIENTATION 0x0002
#define KB800X_ORIENTATION_POLARITY 0x1
#define KB800X_ORIENTATION_DP_UFP 0x4
#define KB800X_ORIENTATION_DP_DFP 0x6
#define KB800X_ORIENTATION_CIO_LANE_SWAP 0x8
/* Select one, 0x0 for non-legacy */
#define KB800X_ORIENTATION_CIO_LEGACY_PASSIVE (0x1 << 4)
#define KB800X_ORIENTATION_CIO_LEGACY_UNIDIR (0x2 << 4)
#define KB800X_ORIENTATION_CIO_LEGACY_BIDIR (0x3 << 4)

#define KB800X_REG_RESET 0x0006
#define KB800X_RESET_FSM BIT(0)
#define KB800X_RESET_MM BIT(1)
#define KB800X_RESET_SERDES BIT(2)
#define KB800X_RESET_COM BIT(3)
#define KB800X_RESET_MASK GENMASK(3, 0)

#define KB800X_REG_XBAR_OVR 0x5040
#define KB800X_XBAR_OVR_EN BIT(6)

/* Registers to configure the elastic buffer input connection */
#define KB800X_REG_XBAR_EB1SEL 0x5044
#define KB800X_REG_XBAR_EB23SEL 0x5045
#define KB800X_REG_XBAR_EB4SEL 0x5046
#define KB800X_REG_XBAR_EB56SEL 0x5047

/* Registers to configure the elastic buffer output connection (x=0-7) */
#define KB800X_REG_TXSEL_FROM_PHY(x) (0x5048 + ((x) / 2))

enum kb800x_ss_lane { KB800X_TX0 = 0, KB800X_TX1, KB800X_RX0, KB800X_RX1 };

enum kb800x_phy_lane {
	KB800X_A0 = 0,
	KB800X_A1,
	KB800X_B0,
	KB800X_B1,
	KB800X_C0,
	KB800X_C1,
	KB800X_D0,
	KB800X_D1,
	KB800X_PHY_LANE_COUNT
};

enum kb800x_eb {
	KB800X_EB1 = 0,
	KB800X_EB2,
	KB800X_EB3,
	KB800X_EB4,
	KB800X_EB5,
	KB800X_EB6
};

#define KB800X_FLIP_SS_LANE(x) ((x) + 1 - 2 * ((x) & 0x1))
#define KB800X_LANE_NUMBER_FROM_PHY(x) ((x) & 0x1)
#define KB800X_PHY_IS_AB(x) ((x) <= KB800X_B1)

struct kb800x_control_t {
	enum gpio_signal retimer_rst_gpio;
	enum gpio_signal usb_ls_en_gpio;
#ifdef CONFIG_KB800X_CUSTOM_XBAR
	enum kb800x_ss_lane ss_lanes[KB800X_PHY_LANE_COUNT];
#endif /* CONFIG_KB800X_CUSTOM_XBAR */
};

/*
 * Default 'example' lane mapping. With this mapping, CONFIG_KB800X_CUSTOM_XBAR
 * can be undefined, since a custom xbar mapping is not needed.
 * ss_lanes = {
 * [KB800X_A0] = KB800X_TX0, [KB800X_A1] = KB800X_RX0,
 * [KB800X_B0] = KB800X_RX1, [KB800X_B1] = KB800X_TX1,
 * [KB800X_C0] = KB800X_RX0, [KB800X_C1] = KB800X_TX0,
 * [KB800X_D0] = KB800X_TX1, [KB800X_D1] = KB800X_RX1,}
 */

extern struct kb800x_control_t kb800x_control[];

#endif /* __CROS_EC_KB800X_H  */
