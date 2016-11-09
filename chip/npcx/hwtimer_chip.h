/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef __CROS_EC_HWTIMER_CHIP_H
#define __CROS_EC_HWTIMER_CHIP_H

/* Channel definition for ITIM */
#define ITIM_EVENT_NO	ITIM16_1
#define ITIM_WDG_NO	ITIM16_5

/* Use ITIM32 as main hardware timer */
#define TICK_ITIM32_MAX_CNT  0xFFFFFFFF
/* Maximum deadline of event */
#define EVT_MAX_EXPIRED_US   TICK_ITIM32_MAX_CNT

/* Clock source for ITIM16 */
enum ITIM_SOURCE_CLOCK_T {
	ITIM_SOURCE_CLOCK_APB2 = 0,
	ITIM_SOURCE_CLOCK_32K  = 1,
};

/* Initialize ITIM16 timer */
void init_hw_timer(int itim_no, enum ITIM_SOURCE_CLOCK_T source);

/* Returns the counter value of event timer */
uint16_t __hw_clock_event_count(void);

/* Returns time delay because of deep idle */
uint32_t __hw_clock_get_sleep_time(uint16_t pre_evt_cnt);

#endif /* __CROS_EC_HWTIMER_CHIP_H */
