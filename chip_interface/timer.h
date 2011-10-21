/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * timer.h - periodical timer
 */

#ifndef __CHIP_INTERFACE_TIMER_H
#define __CHIP_INTERFACE_TIMER_H

/* init hardware and prepare ISR */
EcError EcPeriodicalTimerInit(void);

EcError EcPeriodicalTimerRegister(
    int interval  /* ms */,
    int (*timer)(int /* delta ms from last call */));


#endif  /* __CHIP_INTERFACE_TIMER_H */
