/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-9241 battery charger public header
 */

#ifndef __CROS_EC_DRIVER_CHARGER_ISL9241_PUBLIC_H
#define __CROS_EC_DRIVER_CHARGER_ISL9241_PUBLIC_H

#define ISL9241_ADDR_FLAGS	0x09

/* Default minimum VIN voltage controlled by ISL9241_REG_VIN_VOLTAGE */
#define ISL9241_BC12_MIN_VOLTAGE	4096

extern const struct charger_drv isl9241_drv;

/**
 * Set AC prochot threshold
 *
 * @param chgnum: Index into charger chips
 * @param ma: AC prochot threshold current in mA, multiple of 128mA
 * @return EC_SUCCESS or error
 */
int isl9241_set_ac_prochot(int chgnum, int ma);

/**
 * Set DC prochot threshold
 *
 * @param chgnum: Index into charger chips
 * @param ma: DC prochot threshold current in mA, multiple of 256mA
 * @return EC_SUCCESS or error
 */
int isl9241_set_dc_prochot(int chgnum, int ma);

#define ISL9241_AC_PROCHOT_CURRENT_MIN  128     /* mA */
#define ISL9241_AC_PROCHOT_CURRENT_MAX  6400    /* mA */
#define ISL9241_DC_PROCHOT_CURRENT_MIN  256     /* mA */
#define ISL9241_DC_PROCHOT_CURRENT_MAX  12800   /* mA */

#endif /* __CROS_EC_DRIVER_CHARGER_ISL9241_PUBLIC_H */
