/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Elan touchpad driver for Chrome EC */

#ifndef __CROS_EC_TOUCHPAD_ELAN_H
#define __CROS_EC_TOUCHPAD_ELAN_H

#ifdef CONFIG_TOUCHPAD_ELAN
void elan_tp_interrupt(enum gpio_signal signal);
#else
static inline void elan_tp_interrupt(enum gpio_signal signal) { }
#endif /* !CONFIG_TOUCHPAD_ELAN */

#endif
