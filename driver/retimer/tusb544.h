/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TUSB544 USB Type-C Multi-Protocol Linear Redriver
 */

#ifndef __CROS_EC_USB_REDRIVER_TUSB544_H
#define __CROS_EC_USB_REDRIVER_TUSB544_H

#define TUSB544_I2C_ADDR_FLAGS0 0x44

#define TUSB544_REG_GENERAL4	0x0A
#define TUSB544_GEN4_CTL_SEL	GENMASK(1, 0)
#define TUSB544_GEN4_FLIP_SEL	BIT(2)
#define TUSB544_GEN4_HPDIN	BIT(3)
#define TUSB544_GEN4_EQ_OVRD	BIT(4)
#define TUSB544_GEN4_SWAP_SEL	BIT(5)

enum tusb544_ct_sel {
	TUSB544_CTL_SEL_DISABLED,
	TUSB544_CTL_SEL_USB_ONLY,
	TUSB544_CTL_SEL_DP_ONLY,
	TUSB544_CTL_SEL_DP_USB,
};

#define TUSB544_REG_GENERAL6	0x0C
#define TUSB544_GEN6_DIR_SEL	GENMASK(1, 0)

enum tusb544_dir_sel {
	TUSB544_DIR_SEL_USB_DP_SRC,
	TUSB544_DIR_SEL_USB_DP_SNK,
	TUSB544_DIR_SEL_CUSTOM_SRC,
	TUSB544_DIS_SEL_CUSTOM_SNK,
};

/*
 * Note: TUSB544 automatically snoops DP lanes to enable, but may be manually
 * directed which lanes to turn on when snoop is disabled
 */
#define TUSB544_REG_DP4			0x13
#define TUSB544_DP4_DP0_DISABLE		BIT(0)
#define TUSB544_DP4_DP1_DISABLE		BIT(1)
#define TUSB544_DP4_DP2_DISABLE		BIT(2)
#define TUSB544_DP4_DP3_DISABLE		BIT(3)
#define TUSB544_DP4_AUX_SBU_OVR		GENMASK(5, 4)
#define TUSB544_DP4_AUX_SNOOP_DISABLE	BIT(7)

extern const struct usb_mux_driver tusb544_drv;

#endif
