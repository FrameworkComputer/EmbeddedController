/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(LID_OPEN,             0, 0, GPIO_INT_BOTH, lid_interrupt)
GPIO_INT(POWER_BUTTON_L,       0, 0, GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(AC_PRESENT,           0, 0, GPIO_INT_BOTH, extpower_interrupt)
GPIO_INT(BUTTON_VOLUME_DOWN_L, 0, 0, GPIO_INT_BOTH, button_interrupt)
GPIO_INT(BUTTON_VOLUME_UP,     0, 0, GPIO_INT_BOTH, button_interrupt)
GPIO_INT(CHARGE_DONE,          0, 0, GPIO_INT_BOTH, inductive_charging_interrupt)

GPIO(EC_INT,               0, 0, 0)
GPIO(WP,                   0, 0, 0)
GPIO(ENTERING_RW,          0, 0, 0)
GPIO(PCH_BKLTEN,           0, 0, 0)
GPIO(ENABLE_BACKLIGHT,     0, 0, 0)

/* Inductive charging */
GPIO(CHARGE_EN,            0, 0, 0)
GPIO(BASE_CHG_VDD_EN,      0, 0, 0)
