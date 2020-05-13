/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Register map */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

#define DUMMY_GPIO_BANK 0

#define SCP_REG_BASE			0x70000000

/* memory remap */
#define SCP_R_REMAP_0X0123		REG32(SCP_REG_BASE + 0xA5060)
#define SCP_R_REMAP_0X4567		REG32(SCP_REG_BASE + 0xA5064)
#define SCP_R_REMAP_0X89AB		REG32(SCP_REG_BASE + 0xA5068)
#define SCP_R_REMAP_0XCDEF		REG32(SCP_REG_BASE + 0xA506C)

#endif /* __CROS_EC_REGISTERS_H */
