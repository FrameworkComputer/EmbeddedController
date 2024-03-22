/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Buccaneer board configuration */

#include "base_board.h"

#ifndef __CROS_EC_BOARD_BUCCANEER_BOARD_H
#define __CROS_EC_BOARD_BUCCANEER_BOARD_H

/*
 *-------------------------------------------------------------------------*
 * Fingerprint Specific
 *-------------------------------------------------------------------------*
 */

#ifdef SECTION_IS_RW
#define CONFIG_FP_SENSOR_ELAN80SG
#endif /* SECTION_IS_RW */

#endif /* __CROS_EC_BOARD_BUCCANEER_BOARD_H */
