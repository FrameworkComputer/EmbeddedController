/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef HWTIMER_CHIP_H_
#define HWTIMER_CHIP_H_

/* Channel definition for ITIM16 */
#define ITIM_TIME_NO	ITIM16_1
#define ITIM_EVENT_NO	ITIM16_2
#define ITIM_WDG_NO	ITIM16_5

/* Clock source for ITIM16 */
enum ITIM16_SOURCE_CLOCK_T {
	ITIM16_SOURCE_CLOCK_APB2 = 0,
	ITIM16_SOURCE_CLOCK_32K  = 1,
};

/* Initialize ITIM16 timer */
void init_hw_timer(int itim_no, enum ITIM16_SOURCE_CLOCK_T source);

/* Returns time delay cause of deep idle */
uint32_t __hw_clock_get_sleep_time(void);

#endif /* HWTIMER_CHIP_H_ */
