/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_SERVO_V4_USB_PD_PDO_H
#define __CROS_EC_BOARD_SERVO_V4_USB_PD_PDO_H

#include "compile_time_macros.h"
#include "stdint.h"

extern const uint32_t pd_snk_pdo[3];
extern const int pd_snk_pdo_cnt;

extern const uint16_t pd_src_voltages_mv[6];
extern uint32_t pd_src_chg_pdo[ARRAY_SIZE(pd_src_voltages_mv)];
extern uint8_t chg_pdo_cnt;

extern int active_charge_port;

struct vbus_prop {
	int mv;
	int ma;
};
extern struct vbus_prop vbus[CONFIG_USB_PD_PORT_MAX_COUNT];

int charge_port_is_active(void);
int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port);

#endif /* __CROS_EC_BOARD_SERVO_V4_USB_PD_PDO_H */
