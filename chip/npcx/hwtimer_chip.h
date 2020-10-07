/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific hwtimer module for Chrome EC */

#ifndef __CROS_EC_HWTIMER_CHIP_H
#define __CROS_EC_HWTIMER_CHIP_H

/* Use ITIM32 as main hardware timer */
#define TICK_ITIM32_MAX_CNT  0xFFFFFFFF
/* Maximum deadline of event */
#define EVT_MAX_EXPIRED_US   TICK_ITIM32_MAX_CNT

/* Clock source for ITIM16 */
enum ITIM_SOURCE_CLOCK_T {
	ITIM_SOURCE_CLOCK_APB2 = 0,
	ITIM_SOURCE_CLOCK_32K  = 1,
};

/**
 * Initialise a hardware timer
 *
 * Select the source clock for a timer and prepare it for use.
 *
 * @param itim_no	Timer number to init (enum ITIM_MODULE_T)
 * @param source	Source for timer clock (enum ITIM_SOURCE_CLOCK_T)
 */
void init_hw_timer(int itim_no, enum ITIM_SOURCE_CLOCK_T source);

/* Returns the counter value of event timer */
uint16_t __hw_clock_event_count(void);

/* Returns time delay because of deep idle */
uint32_t __hw_clock_get_sleep_time(uint16_t pre_evt_cnt);

/* Handle ITIM32 overflow if interrupt is disabled */
void __hw_clock_handle_overflow(uint32_t clksrc_high);

/**
 * Set up the timer for use before the task system is available
 *
 * @param start_t	Value to assign to the counter
 */
void __hw_early_init_hwtimer(uint32_t start_t);

#endif /* __CROS_EC_HWTIMER_CHIP_H */
