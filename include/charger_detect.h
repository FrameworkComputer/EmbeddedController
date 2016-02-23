/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Detect what adapter is connected */

#ifndef __CROS_CHARGER_DETECT_H
#define __CROS_CHARGER_DETECT_H

/*
 * Get attached device type.
 *
 * @return CHARGE_SUPPLIER_BC12_* or 0 if the device type was not detected
 */
int charger_detect_get_device_type(void);

#endif /* __CROS_CHARGER_DETECT_H */
