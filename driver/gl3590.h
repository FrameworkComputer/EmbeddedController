/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "pwr_defs.h"

/* Registers definitions */
#define GL3590_HUB_MODE_REG		0x0
#define GL3590_HUB_MODE_USB2_EN		0x2
#define GL3590_HUB_MODE_USB3_EN		0x4
#define GL3590_INT_REG			0x1
#define GL3590_INT_PENDING		0x1
#define GL3590_INT_CLEAR		0x1
#define GL3590_RESPONSE_REG		0x2
#define GL3590_RESPONSE_REG_SYNC_MASK	0x80
#define GL3590_HUB_STS_REG 		0xA
#define GL3590_HUB_STS_HOST_PWR_MASK	0x30
#define GL3590_HUB_STS_HOST_PWR_SHIFT	4
#define GL3590_DEFAULT_HOST_PWR_SRC	0x0
#define GL3590_1_5_A_HOST_PWR_SRC	0x1
#define GL3590_3_0_A_HOST_PWR_SRC	0x2

#define GL3590_I2C_ADDR0 0x50

/* Read GL3590 I2C register */
int gl3590_read(int hub, uint8_t reg, uint8_t *data, int count);

/* Write to GL3590 I2C register */
int gl3590_write(int hub, uint8_t reg, uint8_t *data, int count);

/* Generic handler for GL3590 IRQ, can be registered/invoked by platform */
void gl3590_irq_handler(int hub);

/* Get power capabilities of UFP host connection */
enum ec_error_list gl3590_ufp_pwr(int hub, struct pwr_con_t *pwr);

/* Generic USB HUB I2C interface */
struct uhub_i2c_iface_t {
	int i2c_host_port;
	int i2c_addr;
};
extern struct uhub_i2c_iface_t uhub_config[];
