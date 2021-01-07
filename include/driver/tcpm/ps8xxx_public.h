/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Parade Tech Type-C port controller */

#ifndef __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H
#define __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H

/* I2C interface */
#define PS8751_I2C_ADDR1_P1_FLAGS 0x09
#define PS8751_I2C_ADDR1_P2_FLAGS 0x0A
#define PS8751_I2C_ADDR1_FLAGS    0x0B	/* P3 */
#define PS8751_I2C_ADDR2_FLAGS    0x1B
#define PS8751_I2C_ADDR3_FLAGS    0x2B
#define PS8751_I2C_ADDR4_FLAGS    0x4B

#define PS8XXX_VENDOR_ID  0x1DA0

/* Minimum Delay for reset assertion */
#define PS8XXX_RESET_DELAY_MS 1

/* Delay between releasing reset and the first I2C read */
#define PS8805_FW_INIT_DELAY_MS 10

/* Delay from power on to reset de-asserted */
#define PS8815_PWR_H_RST_H_DELAY_MS 20
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
#define PS8815_FW_INIT_DELAY_MS 40

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
__override_proto
uint16_t board_get_ps8xxx_product_id(int port);

void ps8xxx_tcpc_update_hpd_status(const struct usb_mux *me,
				   int hpd_lvl, int hpd_irq);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
extern struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev;
#endif /* defined(CONFIG_CMD_I2C_STRESS_TEST_TCPC) */

#endif /* __CROS_EC_DRIVER_TCPM_PS8XXX_PUBLIC_H */
