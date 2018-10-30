/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module. */

#include "clock.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer_chip.h"
#include "math_util.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

#define TACH_EC_FREQ                8000000
#define FAN_CTRL_BASED_MS           10
#define FAN_CTRL_INTERVAL_MAX_MS    60

/* The sampling rate (fs) is FreqEC / 128 */
#define TACH_DATA_VALID_TIMEOUT_MS  (0xFFFF * 128 / (TACH_EC_FREQ / 1000))

/*
 * Fan Speed (RPM) = 60 / (1/fs sec * {FnTMRR, FnTLRR} * P)
 * n denotes 1 or 2.
 * P denotes the numbers of square pulses per revolution.
 * And {FnTMRR, FnTLRR} = 0000h denotes Fan Speed is zero.
 * The sampling rate (fs) is FreqEC / 128.
 */
/* pulse, the numbers of square pulses per revolution. */
#define TACH0_TO_RPM(pulse, raw) (60 * TACH_EC_FREQ / 128 / pulse / raw)
#define TACH1_TO_RPM(pulse, raw) (raw * 120 / (pulse * 2))

enum fan_output_s {
	FAN_DUTY_I  = 0x01,
	FAN_DUTY_R  = 0x02,
	FAN_DUTY_OV = 0x03,
	FAN_DUTY_DONE = 0x04,
};

struct fan_info {
	unsigned int    flags;
	int             fan_mode;
	int             fan_p;
	int             rpm_target;
	int             rpm_actual;
	int             tach_valid_ms;
	int             rpm_re;
	int             fan_ms;
	int             fan_ms_idx;
	int             startup_duty;
	enum fan_status fan_sts;
	int             enabled;
};
static struct fan_info fan_info_data[TACH_CH_COUNT];

static enum tach_ch_sel tach_bind(int ch)
{
	return fan_tach[pwm_channels[ch].channel].ch_tach;
}

static void fan_set_interval(int ch)
{
	int diff, fan_ms;
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	diff = ABS(fan_info_data[tach_ch].rpm_target -
		fan_info_data[tach_ch].rpm_actual) / 100;

	fan_ms = FAN_CTRL_INTERVAL_MAX_MS;

	fan_ms -= diff;
	if (fan_ms < FAN_CTRL_BASED_MS)
		fan_ms = FAN_CTRL_BASED_MS;

	fan_info_data[tach_ch].fan_ms = fan_ms;
}

static void fan_init_start(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		fan_set_duty(ch, fan_info_data[tach_ch].startup_duty);
}

static int fan_all_disabled(void)
{
	int fan, all_disabled = 0;

	for (fan = 0; fan < fan_get_count(); fan++) {
		if (!fan_get_enabled(FAN_CH(fan)))
			all_disabled++;
	}

	if (all_disabled >= fan_get_count())
		return 1;

	return 0;
}

void fan_set_enabled(int ch, int enabled)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	/* enable */
	if (enabled) {
		if (tach_ch < TACH_CH_COUNT)
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_CHANGING;

		disable_sleep(SLEEP_MASK_FAN);
		/* enable timer interrupt for fan control */
		ext_timer_start(FAN_CTRL_EXT_TIMER, 1);
	/* disable */
	} else {
		fan_set_duty(ch, 0);

		if (tach_ch < TACH_CH_COUNT) {
			fan_info_data[tach_ch].rpm_actual = 0;
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_STOPPED;
		}
	}

	/* on/off */
	if (tach_ch < TACH_CH_COUNT) {
		fan_info_data[tach_ch].enabled = enabled;
		fan_info_data[tach_ch].tach_valid_ms = 0;
	}

	pwm_enable(ch, enabled);

	if (!enabled) {
		/* disable timer interrupt if all fan off. */
		if (fan_all_disabled()) {
			ext_timer_stop(FAN_CTRL_EXT_TIMER, 1);
			enable_sleep(SLEEP_MASK_FAN);
		}
	}
}

int fan_get_enabled(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		return pwm_get_enabled(ch) && fan_info_data[tach_ch].enabled;
	else
		return 0;
}

void fan_set_duty(int ch, int percent)
{
	pwm_set_duty(ch, percent);
}

int fan_get_duty(int ch)
{
	return pwm_get_duty(ch);
}

int fan_get_rpm_mode(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		return fan_info_data[tach_ch].fan_mode;
	else
		return EC_ERROR_UNKNOWN;
}

void fan_set_rpm_mode(int ch, int rpm_mode)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		fan_info_data[tach_ch].fan_mode = rpm_mode;
}

int fan_get_rpm_actual(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		return fan_info_data[tach_ch].rpm_actual;
	else
		return EC_ERROR_UNKNOWN;
}

int fan_get_rpm_target(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		return fan_info_data[tach_ch].rpm_target;
	else
		return EC_ERROR_UNKNOWN;
}

test_mockable void fan_set_rpm_target(int ch, int rpm)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		fan_info_data[tach_ch].rpm_target = rpm;
}

enum fan_status fan_get_status(int ch)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		return fan_info_data[tach_ch].fan_sts;
	else
		return FAN_STATUS_STOPPED;
}

/**
 * Return non-zero if fan is enabled but stalled.
 */
int fan_is_stalled(int ch)
{
	/* Must be enabled with non-zero target to stall */
	if (!fan_get_enabled(ch) ||
		fan_get_rpm_target(ch) == 0 ||
		!fan_get_duty(ch))
		return 0;

	/* Check for stall condition */
	return fan_get_status(ch) == FAN_STATUS_STOPPED;
}

void fan_channel_setup(int ch, unsigned int flags)
{
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	if (tach_ch < TACH_CH_COUNT)
		fan_info_data[tach_ch].flags = flags;
}

static void fan_ctrl(int ch)
{
	int status = -1, adjust = 0;
	int rpm_actual, rpm_target, rpm_re, duty;
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	fan_info_data[tach_ch].fan_ms_idx += FAN_CTRL_BASED_MS;
	if (fan_info_data[tach_ch].fan_ms_idx >
		fan_info_data[tach_ch].fan_ms) {
		fan_info_data[tach_ch].fan_ms_idx = 0x00;
		adjust = 1;
	}

	if (adjust) {
		/* get current pwm output duty */
		duty = fan_get_duty(ch);

		/* rpm mode */
		if (fan_info_data[tach_ch].fan_mode) {
			rpm_actual = fan_info_data[tach_ch].rpm_actual;
			rpm_target = fan_info_data[tach_ch].rpm_target;
			rpm_re = fan_info_data[tach_ch].rpm_re;

			if (rpm_actual < (rpm_target - rpm_re)) {
				if (duty == 100) {
					status = FAN_DUTY_OV;
				} else {
					if (duty == 0)
						fan_init_start(ch);

					pwm_duty_inc(ch);
					status = FAN_DUTY_I;
				}
			} else if (rpm_actual > (rpm_target + rpm_re)) {
				if (duty == 0) {
					status = FAN_DUTY_OV;
				} else {
					pwm_duty_reduce(ch);
					status = FAN_DUTY_R;
				}
			} else {
				status = FAN_DUTY_DONE;
			}
		} else {
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_LOCKED;
		}

		if (status == FAN_DUTY_DONE) {
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_LOCKED;
		} else if ((status == FAN_DUTY_I) || (status == FAN_DUTY_R)) {
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_CHANGING;
		} else if (status == FAN_DUTY_OV) {
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_FRUSTRATED;

			if (!fan_info_data[tach_ch].rpm_actual && duty)
				fan_info_data[tach_ch].fan_sts =
					FAN_STATUS_STOPPED;
		}
	}
}

static int tach_ch_valid(enum tach_ch_sel tach_ch)
{
	int valid = 0;

	switch (tach_ch) {
	case TACH_CH_TACH0A:
		if ((IT83XX_PWM_TSWCTRL & 0x0C) == 0x08)
			valid = 1;
		break;
	case TACH_CH_TACH1A:
		if ((IT83XX_PWM_TSWCTRL & 0x03) == 0x02)
			valid = 1;
		break;
	case TACH_CH_TACH0B:
		if ((IT83XX_PWM_TSWCTRL & 0x0C) == 0x0C)
			valid = 1;
		break;
	case TACH_CH_TACH1B:
		if ((IT83XX_PWM_TSWCTRL & 0x03) == 0x03)
			valid = 1;
		break;
	default:
		break;
	}

	return valid;
}

static int get_tach0_rpm(int fan_p)
{
	uint16_t rpm;

	/* TACH0A / TACH0B data is valid */
	if (IT83XX_PWM_TSWCTRL & 0x08) {
		rpm = (IT83XX_PWM_F1TMRR << 8) | IT83XX_PWM_F1TLRR;

		if (rpm)
			rpm = TACH0_TO_RPM(fan_p, rpm);

		/* W/C */
		IT83XX_PWM_TSWCTRL |= 0x08;
		return rpm;
	}
	return -1;
}

static int get_tach1_rpm(int fan_p)
{
	uint16_t rpm;

	/* TACH1A / TACH1B data is valid */
	if (IT83XX_PWM_TSWCTRL & 0x02) {
		rpm = (IT83XX_PWM_F2TMRR << 8) | IT83XX_PWM_F2TLRR;

		if (rpm)
			rpm = TACH1_TO_RPM(fan_p, rpm);

		/* W/C */
		IT83XX_PWM_TSWCTRL |= 0x02;
		return rpm;
	}
	return -1;
}

static void proc_tach(int ch)
{
	int t_rpm;
	enum tach_ch_sel tach_ch;

	tach_ch = tach_bind(ch);

	/* tachometer data valid */
	if (tach_ch_valid(tach_ch)) {
		if ((tach_ch == TACH_CH_TACH0A) || (tach_ch == TACH_CH_TACH0B))
			t_rpm = get_tach0_rpm(fan_info_data[tach_ch].fan_p);
		else
			t_rpm = get_tach1_rpm(fan_info_data[tach_ch].fan_p);

		fan_info_data[tach_ch].rpm_actual = t_rpm;
		fan_set_interval(ch);
		fan_info_data[tach_ch].tach_valid_ms = 0;
	} else {
		fan_info_data[tach_ch].tach_valid_ms += FAN_CTRL_BASED_MS;
		if (fan_info_data[tach_ch].tach_valid_ms >
			TACH_DATA_VALID_TIMEOUT_MS)
			fan_info_data[tach_ch].rpm_actual = 0;
	}
}

void fan_ext_timer_interrupt(void)
{
	int fan;

	task_clear_pending_irq(et_ctrl_regs[FAN_CTRL_EXT_TIMER].irq);

	for (fan = 0; fan < fan_get_count(); fan++) {
		if (fan_get_enabled(FAN_CH(fan))) {
			proc_tach(FAN_CH(fan));
			fan_ctrl(FAN_CH(fan));
		}
	}
}

static void fan_init(void)
{
	int ch, rpm_re, fan_p, s_duty;
	enum tach_ch_sel tach_ch;

	for (ch = 0; ch < fan_get_count(); ch++) {

		rpm_re = fan_tach[pwm_channels[FAN_CH(ch)].channel].rpm_re;
		fan_p = fan_tach[pwm_channels[FAN_CH(ch)].channel].fan_p;
		s_duty = fan_tach[pwm_channels[FAN_CH(ch)].channel].s_duty;
		tach_ch = tach_bind(FAN_CH(ch));

		if (tach_ch < TACH_CH_COUNT) {

			if (tach_ch == TACH_CH_TACH0B) {
				/* GPJ2 will select TACH0B as its alt. */
				IT83XX_GPIO_GRC5 |= 0x01;
				/* bit2, to select TACH0B */
				IT83XX_PWM_TSWCTRL |= 0x04;
			} else if (tach_ch == TACH_CH_TACH1B) {
				/* GPJ3 will select TACH1B as its alt. */
				IT83XX_GPIO_GRC5 |= 0x02;
				/* bit0, to select TACH1B */
				IT83XX_PWM_TSWCTRL |= 0x01;
			}

			fan_info_data[tach_ch].flags = 0;
			fan_info_data[tach_ch].fan_mode = 0;
			fan_info_data[tach_ch].rpm_target = 0;
			fan_info_data[tach_ch].rpm_actual = 0;
			fan_info_data[tach_ch].tach_valid_ms = 0;
			fan_info_data[tach_ch].fan_ms_idx = 0;
			fan_info_data[tach_ch].enabled = 0;
			fan_info_data[tach_ch].fan_p = fan_p;
			fan_info_data[tach_ch].rpm_re = rpm_re;
			fan_info_data[tach_ch].fan_ms = FAN_CTRL_BASED_MS;
			fan_info_data[tach_ch].fan_sts = FAN_STATUS_STOPPED;
			fan_info_data[tach_ch].startup_duty = s_duty;
		}
	}

	/* init external timer for fan control */
	ext_timer_ms(FAN_CTRL_EXT_TIMER, EXT_PSR_32P768K_HZ, 0, 0,
			FAN_CTRL_BASED_MS, 1, 0);
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_INIT_FAN);
