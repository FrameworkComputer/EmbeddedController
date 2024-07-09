/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_board.h"

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 *-------------------------------------------------------------------------*
 * Fingerprint Specific
 *-------------------------------------------------------------------------*
 */

#ifdef SECTION_IS_RW

#define CONFIG_FP_SENSOR_FPC1025

#define CONFIG_LIB_DRUID
#define CONFIG_LIB_DRUID_WRAPPER
#define CONFIG_LIB_DRUID_ARMV7

#define FILTERSIZE 25

#endif /* SECTION_IS_RW */

#endif /* __CROS_EC_BOARD_H */
