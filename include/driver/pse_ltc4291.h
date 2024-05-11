/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The LTC4291 is a power over ethernet (PoE) power sourcing equipment (PSE)
 * controller.
 */

#ifndef __CROS_EC_DRIVER_PSE_LTC4291_H
#define __CROS_EC_DRIVER_PSE_LTC4291_H

#include "i2c.h"
#include "timer.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LTC4291_I2C_ADDR 0x2C

#define LTC4291_REG_SUPEVN_COR 0x0B
#define LTC4291_REG_STATPWR 0x10
#define LTC4291_REG_STATPIN 0x11
#define LTC4291_REG_OPMD 0x12
#define LTC4291_REG_DISENA 0x13
#define LTC4291_REG_DETENA 0x14
#define LTC4291_REG_DETPB 0x18
#define LTC4291_REG_PWRPB 0x19
#define LTC4291_REG_RSTPB 0x1A
#define LTC4291_REG_ID 0x1B
#define LTC4291_REG_DEVID 0x43
#define LTC4291_REG_HPMD1 0x46
#define LTC4291_REG_HPMD2 0x4B
#define LTC4291_REG_HPMD3 0x50
#define LTC4291_REG_HPMD4 0x55
#define LTC4291_REG_LPWRPB 0x6E

#define LTC4291_FLD_STATPIN_AUTO BIT(0)
#define LTC4291_FLD_RSTPB_RSTALL BIT(4)

#define LTC4291_STATPWR_ON_PORT(port) (0x01 << (port))
#define LTC4291_DETENA_EN_PORT(port) (0x11 << (port))
#define LTC4291_DETPB_EN_PORT(port) (0x11 << (port))
#define LTC4291_PWRPB_OFF_PORT(port) (0x10 << (port))

#define LTC4291_OPMD_AUTO 0xFF
#define LTC4291_DISENA_ALL 0x0F
#define LTC4291_DETENA_ALL 0xFF
#define LTC4291_ID 0x64
#define LTC4291_DEVID 0x38
#define LTC4291_HPMD_MIN 0x00
#define LTC4291_HPMD_MAX 0xA8

#define LTC4291_PORT_MAX 4

#define LTC4291_RESET_DELAY_US (20 * MSEC)

#define I2C_PSE_READ(reg, data) \
	i2c_read8(I2C_PORT_PSE, LTC4291_I2C_ADDR, LTC4291_REG_##reg, (data))

#define I2C_PSE_WRITE(reg, data) \
	i2c_write8(I2C_PORT_PSE, LTC4291_I2C_ADDR, LTC4291_REG_##reg, (data))

extern const int pse_port_hpmd[LTC4291_PORT_MAX];

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_PSE_LTC4291_H */