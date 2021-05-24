/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Texas Instruments TDP142 DisplayPort Linear Redriver
 */

#ifndef __CROS_EC_REDRIVER_TDP142_H
#define __CROS_EC_REDRIVER_TDP142_H

#include "compile_time_macros.h"

/*
 * Note: Since DP redrivers do not have a standard EC structure, define a
 * TDP142_I2C_PORT and TDP142_I2C_ADDR in board.h
 */
#define TDP142_I2C_ADDR0 0x44
#define TDP142_I2C_ADDR1 0x47
#define TDP142_I2C_ADDR2 0x0C
#define TDP142_I2C_ADDR3 0x0F

/* Registers */
#define TDP142_REG_GENERAL		0x0A
#define TDP142_GENERAL_CTLSEL		GENMASK(1, 0)
#define TDP142_GENERAL_HPDIN_OVRRIDE	BIT(3)
#define TDP142_GENERAL_EQ_OVERRIDE	BIT(4)
#define TDP142_GENERAL_SWAP_HPDIN	BIT(5)

enum tdp142_ctlsel {
	TDP142_CTLSEL_SHUTDOWN,
	TDP142_CTLSEL_DISABLED,
	TDP142_CTLSEL_ENABLED,
};

/* Control redriver enable */
enum ec_error_list tdp142_set_ctlsel(enum tdp142_ctlsel selection);

#endif /* __CROS_EC_REDRIVER_TDP142_H */
