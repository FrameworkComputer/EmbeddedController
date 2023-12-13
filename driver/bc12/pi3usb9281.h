/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB9281 USB port switch.
 */

#ifndef __CROS_EC_PI3USB9281_H
#define __CROS_EC_PI3USB9281_H

#define PI3USB9281_REG_DEV_ID 0x01
#define PI3USB9281_REG_CONTROL 0x02
#define PI3USB9281_REG_INT 0x03
#define PI3USB9281_REG_INT_MASK 0x05
#define PI3USB9281_REG_DEV_TYPE 0x0a
#define PI3USB9281_REG_CHG_STATUS 0x0e
#define PI3USB9281_REG_MANUAL 0x13
#define PI3USB9281_REG_RESET 0x1b
#define PI3USB9281_REG_VBUS 0x1d

#define PI3USB9281_DEV_ID 0x10
#define PI3USB9281_DEV_ID_A 0x18

#define PI3USB9281_CTRL_INT_DIS BIT(0)
#define PI3USB9281_CTRL_AUTO BIT(2)
#define PI3USB9281_CTRL_SWITCH_AUTO BIT(4)
/* Bits 5 thru 7 are read X, write 0 */
#define PI3USB9281_CTRL_MASK 0x1f
/* Bits 1 and 3 are read 1, write 1 */
#define PI3USB9281_CTRL_RSVD_1 0x0a

#define PI3USB9281_PIN_MANUAL_VBUS (3 << 0)
#define PI3USB9281_PIN_MANUAL_DP BIT(2)
#define PI3USB9281_PIN_MANUAL_DM BIT(5)

#define PI3USB9281_INT_ATTACH BIT(0)
#define PI3USB9281_INT_DETACH BIT(1)
#define PI3USB9281_INT_OVP BIT(5)
#define PI3USB9281_INT_OCP BIT(6)
#define PI3USB9281_INT_OVP_OC BIT(7)
#define PI3USB9281_INT_ATTACH_DETACH \
	(PI3USB9281_INT_ATTACH | PI3USB9281_INT_DETACH)

#define PI3USB9281_TYPE_NONE 0
#define PI3USB9281_TYPE_MHL BIT(0)
#define PI3USB9281_TYPE_OTG BIT(1)
#define PI3USB9281_TYPE_SDP BIT(2)
#define PI3USB9281_TYPE_CAR BIT(4)
#define PI3USB9281_TYPE_CDP BIT(5)
#define PI3USB9281_TYPE_DCP BIT(6)

#define PI3USB9281_CHG_NONE 0
#define PI3USB9281_CHG_CAR_TYPE1 BIT(1)
#define PI3USB9281_CHG_CAR_TYPE2 (3 << 0)
#define PI3USB9281_CHG_APPLE_1A BIT(2)
#define PI3USB9281_CHG_APPLE_2A BIT(3)
#define PI3USB9281_CHG_APPLE_2_4A BIT(4)
/* Check if charge status has any connection */
#define PI3USB9281_CHG_STATUS_ANY(x) (((x) & 0x1f) > 1)

/* Define configuration of one pi3usb9281 part */
struct pi3usb9281_config {
	/* i2c port that chip resides on */
	int i2c_port;
	/* GPIO for chip selection in muxed configuration */
	enum gpio_signal mux_gpio;
	/* Logic level of mux_gpio to select chip */
	int mux_gpio_level;
	/* Mutex to lock access to mux gpio or NULL if no mux exists */
	struct mutex *mux_lock;
};

/* Configuration struct defined at board level */
extern struct pi3usb9281_config pi3usb9281_chips[];

/* Enable interrupts. */
int pi3usb9281_enable_interrupts(int port);

/* Get the device type */
int pi3usb9281_get_device_type(int port);

extern const struct bc12_drv pi3usb9281_drv;
#endif /* __CROS_EC_PI3USB9281_H */
