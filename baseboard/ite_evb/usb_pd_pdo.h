/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BASEBOARD_ITE_EVB_USB_PD_PDO_H
#define __CROS_EC_BASEBOARD_ITE_EVB_USB_PD_PDO_H

#include "config.h"
#include "stdint.h"

/* Threshold voltage of VBUS provided (mV) */
#define PD_VBUS_PROVIDED_THRESHOLD 3900

extern const uint32_t pd_src_pdo[1];
extern const int pd_src_pdo_cnt;
extern const uint32_t pd_snk_pdo[3];
extern const int pd_snk_pdo_cnt;

#endif /* __CROS_EC_BASEBOARD_ITE_EVB_USB_PD_PDO_H */
