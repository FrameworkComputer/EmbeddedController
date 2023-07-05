/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* External timers control module for IT83xx. */

#ifndef __CROS_EC_HWTIMER_CHIP_H
#define __CROS_EC_HWTIMER_CHIP_H

#define TIMER_COUNT_1US_SHIFT 3

/* Microseconds to event timer counter setting register */
#define EVENT_TIMER_US_TO_COUNT(us) ((us) << TIMER_COUNT_1US_SHIFT)
/* Event timer counter observation value to microseconds */
#define EVENT_TIMER_COUNT_TO_US(cnt) ((cnt) >> TIMER_COUNT_1US_SHIFT)

#define FREE_EXT_TIMER_L EXT_TIMER_3
#define FREE_EXT_TIMER_H EXT_TIMER_4

/*
 * We only have one free timer, so use it for either fans or CEC. Since ITE also
 * has a CEC peripheral, devices without a fan can have up to two CEC ports, and
 * devices with a fan up to one.
 */
#if defined(CONFIG_FANS) && defined(CONFIG_CEC_BITBANG)
#error "Can't enable both CONFIG_FANS and CONFIG_CEC_BITBANG"
#endif
#if defined(CONFIG_FANS)
#define FAN_CTRL_EXT_TIMER EXT_TIMER_5
#elif defined(CONFIG_CEC_BITBANG)
#define CEC_EXT_TIMER EXT_TIMER_5
#endif

#define EVENT_EXT_TIMER EXT_TIMER_6
/*
 * The low power timer is used to continue system time when EC goes into low
 * power in idle task. Timer 7 is 24bit timer and configured at 32.768khz.
 * The configuration is enough for continuing system time, because periodic
 * tick event (interval is 500ms on it8xxx2) will wake EC up.
 *
 * IMPORTANT:
 * If you change low power timer to a non-24bit timer, you also have to change
 * mask of observation register in clock_sleep_mode_wakeup_isr() or EC will get
 * wrong system time after resume.
 */
#define LOW_POWER_EXT_TIMER EXT_TIMER_7
#define LOW_POWER_TIMER_MASK (BIT(24) - 1)
#define WDT_EXT_TIMER EXT_TIMER_8

enum ext_timer_clock_source {
	EXT_PSR_32P768K_HZ = 0,
	EXT_PSR_1P024K_HZ = 1,
	EXT_PSR_32_HZ = 2,
	EXT_PSR_8M_HZ = 3
};

/*
 * 24-bit timers: external timer 3, 5, and 7
 * 32-bit timers: external timer 4, 6, and 8
 */
enum ext_timer_sel {
	/* timer 3 and 4 combine mode for free running timer */
	EXT_TIMER_3 = 0,
	EXT_TIMER_4,
	/* For fan control */
	EXT_TIMER_5,
	/* timer 6 for event timer */
	EXT_TIMER_6,
	/* For WDT capture important state information before being reset */
	EXT_TIMER_7,
	/* HW timer for low power mode */
	EXT_TIMER_8,
	EXT_TIMER_COUNT,
};

struct ext_timer_ctrl_t {
	volatile uint8_t *mode;
	volatile uint8_t *polarity;
	volatile uint8_t *isr;
	uint8_t mask;
	uint8_t irq;
};

extern const struct ext_timer_ctrl_t et_ctrl_regs[];
#ifdef IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
uint32_t __ram_code ext_observation_reg_read(enum ext_timer_sel ext_timer);
#endif
void ext_timer_start(enum ext_timer_sel ext_timer, int en_irq);
void ext_timer_stop(enum ext_timer_sel ext_timer, int dis_irq);
void fan_ext_timer_interrupt(void);
void cec_ext_timer_interrupt(enum ext_timer_sel ext_timer);
void update_exc_start_time(void);

/**
 * Config a external timer.
 *
 * @param raw	(!=0) timer count equal to param "ms" no conversion.
 */
int ext_timer_ms(enum ext_timer_sel ext_timer,
		 enum ext_timer_clock_source ext_timer_clock, int start,
		 int et_int, int32_t ms, int first_time_enable, int raw);

#endif /* __CROS_EC_HWTIMER_CHIP_H */
