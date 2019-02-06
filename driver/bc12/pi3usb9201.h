/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PI3USB9201 USB BC 1.2 Charger Detector driver definitions */

/* 8-bit I2C address */
#define PI3USB9201_I2C_ADDR_0 0xB8
#define PI3USB9201_I2C_ADDR_1 0xBA
#define PI3USB9201_I2C_ADDR_2 0xBC
#define PI3USB9201_I2C_ADDR_3 0xBE

#define PI3USB9201_REG_CTRL_1 0x0
#define PI3USB9201_REG_CTRL_2 0x1
#define PI3USB9201_REG_CLIENT_STS 0x2
#define PI3USB9201_REG_HOST_STS 0x3

/* Control_1 regiter bit definitions */
#define PI3USB9201_REG_CTRL_1_INT_MASK (1 << 0)
#define PI3USB9201_REG_CTRL_1_MODE (1 << 1)
#define PI3USB9201_REG_CTRL_1_MODE_MASK (0x7 << 1)

/* Control_2 regiter bit definitions */
#define PI3USB9201_REG_CTRL_2_AUTO_SW (1 << 1)
#define PI3USB9201_REG_CTRL_2_START_DET (1 << 3)

/* Host status register bit definitions */
#define PI3USB9201_REG_HOST_STS_BC12_DET (1 << 0)
#define PI3USB9201_REG_HOST_STS_DEV_PLUG (1 << 1)
#define PI3USB9201_REG_HOST_STS_DEV_UNPLUG (1 << 2)

struct pi3usb2901_config_t {
	const int i2c_port;
	const int i2c_addr;
};

/* Configuration struct defined at board level */
extern const struct pi3usb2901_config_t pi3usb2901_bc12_chips[];

