/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PI3USB9201 USB BC 1.2 Charger Detector driver definitions */

/* I2C address */
#define PI3USB9201_I2C_ADDR_0_FLAGS 0x5C
#define PI3USB9201_I2C_ADDR_1_FLAGS 0x5D
#define PI3USB9201_I2C_ADDR_2_FLAGS 0x5E
#define PI3USB9201_I2C_ADDR_3_FLAGS 0x5F

#define PI3USB9201_REG_CTRL_1 0x0
#define PI3USB9201_REG_CTRL_2 0x1
#define PI3USB9201_REG_CLIENT_STS 0x2
#define PI3USB9201_REG_HOST_STS 0x3

/* Flags */
#define PI3USB9201_ALWAYS_POWERED BIT(0)

/* Control_1 regiter bit definitions */
#define PI3USB9201_REG_CTRL_1_INT_MASK BIT(0)
#define PI3USB9201_REG_CTRL_1_MODE_SHIFT 1
#define PI3USB9201_REG_CTRL_1_MODE_MASK (0x7 << \
					 PI3USB9201_REG_CTRL_1_MODE_SHIFT)

/* Control_2 regiter bit definitions */
#define PI3USB9201_REG_CTRL_2_AUTO_SW BIT(1)
#define PI3USB9201_REG_CTRL_2_START_DET BIT(3)

/* Host status register bit definitions */
#define PI3USB9201_REG_HOST_STS_BC12_DET BIT(0)
#define PI3USB9201_REG_HOST_STS_DEV_PLUG BIT(1)
#define PI3USB9201_REG_HOST_STS_DEV_UNPLUG BIT(2)

struct pi3usb9201_config_t {
	const int i2c_port;
	const int i2c_addr_flags;
	const int flags;
};

enum pi3usb9201_mode {
	PI3USB9201_POWER_DOWN,
	PI3USB9201_SDP_HOST_MODE,
	PI3USB9201_DCP_HOST_MODE,
	PI3USB9201_CDP_HOST_MODE,
	PI3USB9201_CLIENT_MODE,
	PI3USB9201_RESERVED_1,
	PI3USB9201_RESERVED_2,
	PI3USB9201_USB_PATH_ON,
};

/* Configuration struct defined at board level */
extern const struct pi3usb9201_config_t pi3usb9201_bc12_chips[];

extern const struct bc12_drv pi3usb9201_drv;
