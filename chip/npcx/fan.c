/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX fan control module. */

#include "clock.h"
#include "clock_chip.h"
#include "console.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "math_util.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#if !(DEBUG_FAN)
#define CPRINTS(...)
#else
#define CPRINTS(format, args...) cprints(CC_PWM, format, ##args)
#endif

/* Tacho measurement state */
enum tacho_measure_state {
	/* Tacho normal state */
	TACHO_NORMAL = 0,
	/* Tacho underflow state */
	TACHO_UNDERFLOW
};

/* Fan mode */
enum tacho_fan_mode {
	/* FAN rpm mode */
	TACHO_FAN_RPM = 0,
	/* FAN duty mode */
	TACHO_FAN_DUTY,
};

/* Fan status data structure */
struct fan_status_t {
	/* Current state of the measurement */
	enum tacho_measure_state cur_state;
	/* Fan mode */
	enum tacho_fan_mode fan_mode;
	/* MFT sampling freq*/
	uint32_t mft_freq;
	/* Actual rpm */
	int rpm_actual;
	/* Target rpm */
	int rpm_target;
	/* Automatic fan status */
	enum fan_status auto_status;
};

/* Global variables */
static volatile struct fan_status_t fan_status[FAN_CH_COUNT];
static int rpm_pre[FAN_CH_COUNT];

/*
 * Fan specifications. If they (PULSES_ROUND and RPM_DEVIATION) cannot meet
 * the followings, please replace them with correct one in board-level driver.
 */

/* Pulses per round */
#ifndef PULSES_ROUND
#define PULSES_ROUND 2 /* 4-phases pwm-type fan. (2-phases should be 1) */
#endif

/* Rpm deviation (Unit:percent) */
#ifndef RPM_DEVIATION
#define RPM_DEVIATION 7
#endif

/*
 * RPM = 60 * f / (n * TACH)
 *   n = Pulses per round
 *   f = Tachometer (MFT) operation freq
 *   TACH = Counts of tachometer
 */
#define TACH_TO_RPM(ch, tach) \
	((fan_status[ch].mft_freq * 60 / PULSES_ROUND) / MAX((tach), 1))

/* MFT TCNT default count */
#define TACHO_MAX_CNT (BIT(16) - 1)

/* Margin of target rpm */
#define RPM_MARGIN(rpm_target) (((rpm_target) * RPM_DEVIATION) / 100)

/**
 * MFT get fan rpm value
 *
 * @param   ch      operation channel
 * @return  actual  rpm
 */
static int mft_fan_rpm(int ch)
{
	volatile struct fan_status_t *p_status = fan_status + ch;
	int mdl = mft_channels[ch].module;
	int tacho;

	/* Check whether MFT underflow flag is occurred */
	if (IS_BIT_SET(NPCX_TECTRL(mdl), NPCX_TECTRL_TCPND)) {
		/* Clear pending flags */
		SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TCCLR);

		/*
		 * Flag TDPND means mft underflow happen,
		 * but let MFT still can re-measure actual rpm
		 * when user change pwm/fan duty during
		 * TACHO_UNDERFLOW state.
		 */
		p_status->cur_state = TACHO_UNDERFLOW;
		p_status->auto_status = FAN_STATUS_STOPPED;
		CPRINTS("Tacho is underflow !");

		return 0;
	}

	/* Check whether MFT capture flag is set, else return previous rpm */
	if (IS_BIT_SET(NPCX_TECTRL(mdl), NPCX_TECTRL_TAPND))
		/* Clear pending flags */
		SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TACLR);
	else
		return p_status->rpm_actual;

	p_status->cur_state = TACHO_NORMAL;
	/*
	 * Start of the last tacho cycle is detected -
	 * calculated tacho cycle duration
	 */
	tacho = TACHO_MAX_CNT - NPCX_TCRA(mdl);
	/* Transfer tacho to actual rpm */
	return (tacho > 0) ? (TACH_TO_RPM(ch, tacho)) : 0;
}

/**
 * Set fan prescaler based on apb1 clock
 *
 * @param   ch      operation channel
 * @return  none
 * @notes   changed when initial or HOOK_FREQ_CHANGE command
 */
void mft_set_apb1_prescaler(int ch)
{
	int mdl = mft_channels[ch].module;
	uint16_t prescaler_divider = 0;

	/* Set clock prescaler divider to MFT module*/
	prescaler_divider =
		(uint16_t)(clock_get_apb1_freq() / fan_status[ch].mft_freq);
	if (prescaler_divider >= 1)
		prescaler_divider = prescaler_divider - 1;
	if (prescaler_divider > 0xFF)
		prescaler_divider = 0xFF;

	NPCX_TPRSC(mdl) = (uint8_t)prescaler_divider;
}

/**
 * Fan configuration.
 *
 * @param ch                        operation channel
 * @param enable_mft_read_rpm       FAN_USE_RPM_MODE enable flag
 * @return none
 */
static void fan_config(int ch, int enable_mft_read_rpm)
{
	int mdl = mft_channels[ch].module;
	int pwm_id = mft_channels[ch].pwm_id;
	enum npcx_mft_clk_src clk_src = mft_channels[ch].clk_src;

	volatile struct fan_status_t *p_status = fan_status + ch;

	/* Setup pwm with fan spec. */
	pwm_config(pwm_id);

	/* Need to initialize MFT or not */
	if (enable_mft_read_rpm) {
		/* Initialize tacho sampling rate */
		if (clk_src == TCKC_LFCLK)
			p_status->mft_freq = INT_32K_CLOCK;
		else if (clk_src == TCKC_PRESCALE_APB1_CLK)
			p_status->mft_freq = clock_get_apb1_freq();
		else
			p_status->mft_freq = 0;

		/* Set mode 5 to MFT module */
		SET_FIELD(NPCX_TMCTRL(mdl), NPCX_TMCTRL_MDSEL_FIELD,
			  NPCX_MFT_MDSEL_5);

		/* Set MFT operation frequency */
		if (clk_src == TCKC_PRESCALE_APB1_CLK)
			mft_set_apb1_prescaler(ch);

		/* Set the low power mode or not. */
		UPDATE_BIT(NPCX_TCKC(mdl), NPCX_TCKC_LOW_PWR,
			   clk_src == TCKC_LFCLK);

		/* Set the default count-down timer. */
		NPCX_TCNT1(mdl) = TACHO_MAX_CNT;
		NPCX_TCRA(mdl) = TACHO_MAX_CNT;

		/* Set the edge polarity to rising. */
		SET_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEDG);
		/* Enable capture TCNT1 into TCRA and preset TCNT1. */
		SET_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEN);
		/* Enable input debounce logic into TA. */
		SET_BIT(NPCX_TCFG(mdl), NPCX_TCFG_TADBEN);

		/* Set the clock source type and start capturing */
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, clk_src);
	}

	/* Set default fan states */
	p_status->cur_state = TACHO_NORMAL;
	p_status->fan_mode = TACHO_FAN_DUTY;
	p_status->auto_status = FAN_STATUS_STOPPED;
}

/**
 * Check all fans are stopped
 *
 * @return   1: all fans are stopped. 0: else.
 */
static int fan_all_disabled(void)
{
	int ch;

	for (ch = 0; ch < fan_get_count(); ch++)
		if (fan_status[ch].auto_status != FAN_STATUS_STOPPED)
			return 0;
	return 1;
}

/**
 * Adjust fan duty by difference between target and actual rpm
 *
 * @param   ch        operation channel
 * @param   rpm_diff  difference between target and actual rpm
 * @param   duty      current fan duty
 */
static void fan_adjust_duty(int ch, int rpm_diff, int duty)
{
	int duty_step = 0;

	/* Find suitable duty step */
	if (ABS(rpm_diff) >= 2000)
		duty_step = 20;
	else if (ABS(rpm_diff) >= 1000)
		duty_step = 10;
	else if (ABS(rpm_diff) >= 500)
		duty_step = 5;
	else if (ABS(rpm_diff) >= 250)
		duty_step = 3;
	else
		duty_step = 1;

	/* Adjust fan duty step by step */
	if (rpm_diff > 0)
		duty = MIN(duty + duty_step, 100);
	else
		duty = MAX(duty - duty_step, 1);

	fan_set_duty(ch, duty);

	CPRINTS("fan%d: duty %d, rpm_diff %d", ch, duty, rpm_diff);
}

/**
 * Smart fan control function.
 *
 * @param   ch         operation channel
 * @param   rpm_actual actual operation rpm value
 * @param   rpm_target target operation rpm value
 * @return  current    fan control status
 */
enum fan_status fan_smart_control(int ch, int rpm_actual, int rpm_target)
{
	int duty, rpm_diff;

	/* wait rpm is stable */
	if (ABS(rpm_actual - rpm_pre[ch]) > RPM_MARGIN(rpm_actual)) {
		rpm_pre[ch] = rpm_actual;
		return FAN_STATUS_CHANGING;
	}

	/*
	 * A specific type of fan needs a longer time to output the TACH
	 * signal to EC after EC outputs the PWM signal to the fan.
	 * During this period, the driver will read two consecutive RPM = 0.
	 * In this case, don't step the PWM duty too aggressively.
	 * See b:225208265 for more detail.
	 */
	if (rpm_pre[ch] == 0 && rpm_actual == 0 &&
	    IS_ENABLED(CONFIG_FAN_BYPASS_SLOW_RESPONSE)) {
		rpm_diff = RPM_MARGIN(rpm_target) + 1;
	} else {
		rpm_diff = rpm_target - rpm_actual;
	}

	/* Record previous rpm */
	rpm_pre[ch] = rpm_actual;
	duty = fan_get_duty(ch);
	if (duty == 0 && rpm_target == 0)
		return FAN_STATUS_STOPPED;

	/* Increase PWM duty */
	if (rpm_diff > RPM_MARGIN(rpm_target)) {
		if (duty == 100)
			return FAN_STATUS_FRUSTRATED;

		fan_adjust_duty(ch, rpm_diff, duty);
		return FAN_STATUS_CHANGING;
		/* Decrease PWM duty */
	} else if (rpm_diff < -RPM_MARGIN(rpm_target)) {
		if (duty == 1 && rpm_target != 0)
			return FAN_STATUS_FRUSTRATED;

		fan_adjust_duty(ch, rpm_diff, duty);
		return FAN_STATUS_CHANGING;
	}

	return FAN_STATUS_LOCKED;
}

/**
 * Tick function for fan control.
 *
 * @return  none
 */
void fan_tick_func(void)
{
	int ch;

	for (ch = 0; ch < FAN_CH_COUNT; ch++) {
		volatile struct fan_status_t *p_status = fan_status + ch;
		/* Make sure rpm mode is enabled */
		if (p_status->fan_mode != TACHO_FAN_RPM) {
			/* Fan in duty mode still want rpm_actual being updated.
			 */
			p_status->rpm_actual = mft_fan_rpm(ch);
			if (p_status->rpm_actual > 0)
				p_status->auto_status = FAN_STATUS_LOCKED;
			else
				p_status->auto_status = FAN_STATUS_STOPPED;
			continue;
		}
		if (!fan_get_enabled(ch))
			continue;
		/* Get actual rpm */
		p_status->rpm_actual = mft_fan_rpm(ch);
		/* Do smart fan stuff */
		p_status->auto_status = fan_smart_control(
			ch, p_status->rpm_actual, p_status->rpm_target);
	}
}
DECLARE_HOOK(HOOK_TICK, fan_tick_func, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* IC specific low-level driver */

/**
 * Set fan duty cycle.
 *
 * @param   ch      operation channel
 * @param   percent duty cycle percent
 * @return  none
 */
void fan_set_duty(int ch, int percent)
{
	int pwm_id = mft_channels[ch].pwm_id;

	/* duty is zero */
	if (!percent) {
		fan_status[ch].auto_status = FAN_STATUS_STOPPED;
		if (fan_all_disabled())
			enable_sleep(SLEEP_MASK_FAN);
	} else
		disable_sleep(SLEEP_MASK_FAN);

	/* Set the duty cycle of PWM */
	pwm_set_duty(pwm_id, percent);
}

/**
 * Get fan duty cycle.
 *
 * @param   ch  operation channel
 * @return  duty cycle
 */
int fan_get_duty(int ch)
{
	int pwm_id = mft_channels[ch].pwm_id;

	/* Return percent */
	return pwm_get_duty(pwm_id);
}
/**
 * Check fan is rpm operation mode.
 *
 * @param   ch  operation channel
 * @return  rpm operation mode or not
 */
int fan_get_rpm_mode(int ch)
{
	return fan_status[ch].fan_mode == TACHO_FAN_RPM ? 1 : 0;
}

/**
 * Set fan to rpm operation mode.
 *
 * @param   ch          operation channel
 * @param   rpm_mode    rpm operation mode flag
 * @return  none
 */
void fan_set_rpm_mode(int ch, int rpm_mode)
{
	if (rpm_mode)
		fan_status[ch].fan_mode = TACHO_FAN_RPM;
	else
		fan_status[ch].fan_mode = TACHO_FAN_DUTY;
}

/**
 * Get fan actual operation rpm.
 *
 * @param   ch  operation channel
 * @return  actual operation rpm value
 */
int fan_get_rpm_actual(int ch)
{
	/* Check PWM is enabled first */
	if (fan_get_duty(ch) == 0)
		return 0;

	CPRINTS("fan %d: get actual rpm = %d", ch, fan_status[ch].rpm_actual);
	return fan_status[ch].rpm_actual;
}

/**
 * Check fan enabled.
 *
 * @param   ch  operation channel
 * @return  enabled or not
 */
int fan_get_enabled(int ch)
{
	int pwm_id = mft_channels[ch].pwm_id;

	return pwm_get_enabled(pwm_id);
}
/**
 * Set fan enabled.
 *
 * @param   ch      operation channel
 * @param   enabled enabled flag
 * @return  none
 */
void fan_set_enabled(int ch, int enabled)
{
	int pwm_id = mft_channels[ch].pwm_id;

	if (!enabled)
		fan_status[ch].auto_status = FAN_STATUS_STOPPED;
	pwm_enable(pwm_id, enabled);
}

/**
 * Get fan setting rpm.
 *
 * @param   ch  operation channel
 * @return  setting rpm value
 */
int fan_get_rpm_target(int ch)
{
	return fan_status[ch].rpm_target;
}

/**
 * Set fan setting rpm.
 *
 * @param   ch  operation channel
 * @param   rpm setting rpm value
 * @return  none
 */
void fan_set_rpm_target(int ch, int rpm)
{
	if (rpm == 0) {
		/* If rpm = 0, disable PWM immediately. Why?*/
		fan_set_duty(ch, 0);
	} else {
		/* This is the counterpart of disabling PWM above. */
		if (!fan_get_enabled(ch))
			fan_set_enabled(ch, 1);
		if (rpm > fans[ch].rpm->rpm_max)
			rpm = fans[ch].rpm->rpm_max;
		else if (rpm < fans[ch].rpm->rpm_min)
			rpm = fans[ch].rpm->rpm_min;
	}

	/* Set target rpm */
	fan_status[ch].rpm_target = rpm;
	CPRINTS("fan %d: set target rpm = %d", ch, fan_status[ch].rpm_target);
}

/**
 * Check fan operation status.
 *
 * @param   ch          operation channel
 * @return  fan_status  fan operation status
 */
enum fan_status fan_get_status(int ch)
{
	return fan_status[ch].auto_status;
}

/**
 * Check fan is stall condition.
 *
 * @param   ch          operation channel
 * @return  non-zero    if fan is enabled but stalled
 */
int fan_is_stalled(int ch)
{
	return fan_get_enabled(ch) && fan_get_duty(ch) &&
	       fan_status[ch].cur_state == TACHO_UNDERFLOW;
}

/**
 * Fan channel setup.
 *
 * @param ch        operation channel
 * @param flags     input flags
 * @return none
 */
void fan_channel_setup(int ch, unsigned int flags)
{
	fan_config(ch, (flags & FAN_USE_RPM_MODE));
}

/**
 * Fan initial.
 *
 * @param none
 * @return none
 */
static void fan_init(void)
{
	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, CGC_FAN_MASK, CGC_MODE_ALL);
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_INIT_FAN);
