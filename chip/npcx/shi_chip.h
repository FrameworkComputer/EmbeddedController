/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SHI module for Chrome EC */

#ifndef __CROS_EC_SHI_CHIP_H_
#define __CROS_EC_SHI_CHIP_H_

/**
 * Called when the NSS level changes, signalling the start of a SHI
 * transaction.
 *
 * @param signal	GPIO signal that changed
 */
void shi_cs_event(enum gpio_signal signal);
#ifdef NPCX_SHI_V2
void shi_cs_gpio_int(enum gpio_signal signal);
#endif

#endif /* __CROS_EC_SHI_CHIP_H_ */
