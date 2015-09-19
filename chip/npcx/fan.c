/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX fan control module. */

#include "clock.h"
#include "clock_chip.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "console.h"
#include "timer.h"
#include "task.h"

#if !(DEBUG_FAN)
#define CPRINTS(...)
#else
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)
#endif

/* MFT model select */
enum npcx_mft_mdsel {
	NPCX_MFT_MDSEL_1,
	NPCX_MFT_MDSEL_2,
	NPCX_MFT_MDSEL_3,
	NPCX_MFT_MDSEL_4,
	NPCX_MFT_MDSEL_5,
	/* Number of MFT modes */
	NPCX_MFT_MDSEL_COUNT
};

/* Tacho measurement state */
enum tacho_measure_state {
	/* Tacho init state */
	TACHO_IN_IDLE = 0,
	/* Tacho first edge state */
	TACHO_WAIT_FOR_1_EDGE,
	/* Tacho second edge state */
	TACHO_WAIT_FOR_2_EDGE,
	/* Tacho underflow state */
	TACHO_UNDERFLOW
};

/* Fan status data structure */
struct fan_status_t {
	/* Current state of the measurement */
	enum tacho_measure_state cur_state;
	/* MFT sampling freq*/
	uint32_t mft_freq;
	/* Actual rpm */
	int rpm_actual;
	/* Target rpm */
	int rpm_target;
};

/* Global variables */
static volatile struct fan_status_t fan_status[FAN_CH_COUNT];

/* Fan encoder spec. */
#define RPM_SCALE    1 /* Fan RPM is multiplier of actual RPM */
#define RPM_EDGES    1 /* Fan number of edges - 1 */
#define POLES        2 /* Pole number of fan */
/* Rounds per second */
#define ROUNDS ((60 / POLES) * RPM_EDGES * RPM_SCALE)
/*
 * RPM = (n - 1) * m * f * 60 / poles / TACH
 *   n = Fan number of edges = (RPM_EDGES + 1)
 *   m = Fan multiplier defined by RANGE
 *   f = PWM and MFT operation freq
 *   poles = 2
 */
#define TACH_TO_RPM(ch, tach) \
	((fan_status[ch].mft_freq * ROUNDS) / MAX((tach), 1))


/**
 * MFT start measure.
 *
 * @param   ch      operation channel
 * @return  none
 */
static void mft_start_measure(int ch)
{
	int mdl = mft_channels[ch].module;

	/* Set the clock source type and start counting */
	SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD,
			mft_channels[ch].clk_src);
}

/**
 * MFT stop measure.
 *
 * @param   ch      operation channel
 * @return  none
 */
static void mft_stop_measure(int ch)
{
	int mdl = mft_channels[ch].module;

	/* Clear all pending flag */
	NPCX_TECLR(mdl) = NPCX_TECTRL(mdl);

	/* Stop the timer and capture events for TCNT2 */
	SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, TCKC_NOCLK);
}

/**
 * MFT final measure.
 *
 * @param   ch      operation channel
 * @return  actual  rpm
 */
static int mft_final_measure(int ch)
{
	int mdl = mft_channels[ch].module;
	int tacho;
	/*
	 * Start of the last tacho cycle is detected -
	 * calculated tacho cycle duration
	 */
	tacho = mft_channels[ch].default_count - NPCX_TCRA(mdl);
	CPRINTS("tacho=%x", tacho);

	/* Transfer tacho to actual rpm */
	return (tacho > 0) ? (TACH_TO_RPM(ch, tacho)) : 0;
}

/**
 * Set fan prescaler based on apb1 clock
 *
 * @param   none
 * @return  none
 * @notes   changed when initial or HOOK_FREQ_CHANGE command
 */
void mft_set_apb1_prescaler(int ch)
{
	int mdl = mft_channels[ch].module;
	uint16_t prescaler_divider = 0;

	/* Set clock prescaler divider to MFT module*/
	prescaler_divider = (uint16_t)(clock_get_apb1_freq()
			/ fan_status[ch].mft_freq);
	if (prescaler_divider >= 1)
		prescaler_divider = prescaler_divider - 1;
	if (prescaler_divider > 0xFF)
		prescaler_divider = 0xFF;

	NPCX_TPRSC(mdl) = (uint8_t) prescaler_divider;
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

	/* Configure pins from GPIOs to FAN */
	gpio_config_module(MODULE_PWM_FAN, 1);

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

		/* Set mode 5 to MFT module*/
		SET_FIELD(NPCX_TMCTRL(mdl), NPCX_TMCTRL_MDSEL_FIELD,
				NPCX_MFT_MDSEL_5);

		/* Set MFT operation frequency */
		if (clk_src == TCKC_PRESCALE_APB1_CLK)
			mft_set_apb1_prescaler(ch);

		/* Set the low power mode or not. */
		UPDATE_BIT(NPCX_TCKC(mdl), NPCX_TCKC_LOW_PWR,
				clk_src == TCKC_LFCLK);

		/* Set the default count-down timer. */
		NPCX_TCNT1(mdl) = mft_channels[ch].default_count;
		NPCX_TCRA(mdl)  = mft_channels[ch].default_count;

		/* Set the edge polarity to rising. */
		SET_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEDG);
		/* Enable capture TCNT1 into TCRA and preset TCNT1. */
		SET_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEN);
		/* Enable input debounce logic into TA. */
		SET_BIT(NPCX_TCFG(mdl), NPCX_TCFG_TADBEN);

		/* Set the no clock to TCNT1. */
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD,
				TCKC_NOCLK);
		/* Set timer wake-up enable */
		SET_BIT(NPCX_TWUEN(mdl), NPCX_TWUEN_TAWEN);
		SET_BIT(NPCX_TWUEN(mdl), NPCX_TWUEN_TCWEN);

	}

	/* Back to Idle mode*/
	p_status->cur_state = TACHO_IN_IDLE;
}

/**
 * Get percentage of duty cycle from rpm
 *
 * @param   ch      operation channel
 * @param   rpm     target rpm
 * @return  none
 */
int fan_rpm_to_percent(int ch, int rpm)
{
	int pct, max, min;

	if (!rpm) {
		pct = 0;
	} else {
		min = fans[ch].rpm_min;
		max = fans[ch].rpm_max;
		pct = (99*rpm + max - 100*min) / (max-min);
	}

	return pct;
}

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

	CPRINTS("set duty percent=%d", percent);
	/* Set the duty cycle of PWM */
	pwm_set_duty(pwm_id, percent);

	/* Start measurement again */
	if (percent != 0 && fan_status[ch].cur_state == TACHO_UNDERFLOW)
		fan_status[ch].cur_state = TACHO_IN_IDLE;
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
 * @notes   not support in npcx chip
 */
int fan_get_rpm_mode(int ch)
{
	/* TODO: (Benson_TBD_4) not support rpm mode, return 0 always */
	return 0;
}

/**
 * Set fan to rpm operation mode.
 *
 * @param   ch          operation channel
 * @param   rpm_mode    rpm operation mode flag
 * @return  none
 * @notes   not support in npcx chip
 */
void fan_set_rpm_mode(int ch, int rpm_mode)
{
	/* TODO: (Benson_TBD_4) not support rpm mode */
}

/**
 * Get fan actual operation rpm.
 *
 * @param   ch  operation channel
 * @return  actual operation rpm value
 */
int fan_get_rpm_actual(int ch)
{
	int mdl = mft_channels[ch].module;
	volatile struct fan_status_t *p_status = fan_status + ch;

	/* Start measure and return previous value when fan is working */
	if ((fan_get_enabled(ch)) && (fan_get_duty(ch))) {
		if (p_status->cur_state == TACHO_IN_IDLE) {
			CPRINTS("mft_startmeasure");
			if (p_status->rpm_actual <= 0)
				p_status->rpm_actual = fans[ch].rpm_min;
			/* Clear all pending flags */
			NPCX_TECLR(mdl) = NPCX_TECTRL(mdl);
			/* Start from first edge state */
			p_status->cur_state = TACHO_WAIT_FOR_1_EDGE;
			/* Start measure */
			mft_start_measure(ch);
		}
		/* Check whether MFT underflow flag is occurred */
		else if (IS_BIT_SET(NPCX_TECTRL(mdl), NPCX_TECTRL_TCPND)) {
			/* Measurement is active - stop the measurement */
			mft_stop_measure(ch);
			/* Need to avoid underflow state happen */
			p_status->rpm_actual = 0;
			/*
			 * Flag TDPND means mft underflow happen,
			 * but let MFT still can re-measure actual rpm
			 * when user change pwm/fan duty during
			 * TACHO_UNDERFLOW state.
			 */
			p_status->cur_state = TACHO_UNDERFLOW;
			CPRINTS("TACHO_UNDERFLOW");

			/* Clear pending flags */
			SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TCCLR);
		}
		/* Check whether MFT signal detection flag is occurred */
		else if (IS_BIT_SET(NPCX_TECTRL(mdl), NPCX_TECTRL_TAPND)) {
			/* Start of tacho cycle is detected */
			switch (p_status->cur_state) {
			case TACHO_WAIT_FOR_1_EDGE:
				CPRINTS("TACHO_WAIT_FOR_1_EDGE");
				/*
				 * Start of the first tacho cycle is detected
				 * and wait for the second tacho cycle
				 * (second edge)
				 */
				p_status->cur_state = TACHO_WAIT_FOR_2_EDGE;
				/* Send previous rpm before complete measure */
				break;
			case TACHO_WAIT_FOR_2_EDGE:
				/* Complete measure tacho and get actual rpm */
				p_status->rpm_actual = mft_final_measure(ch);
				/* Stop the measurement */
				mft_stop_measure(ch);

				/* Back to Idle mode*/
				p_status->cur_state = TACHO_IN_IDLE;
				CPRINTS("TACHO_WAIT_FOR_2_EDGE");
				CPRINTS("rpm_actual=%d", p_status->rpm_actual);
				break;
			default:
				break;
			}
			/* Clear pending flags */
			SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TACLR);
		}
	} else {
		CPRINTS("preset rpm");
		/* Send preset rpm before fan is working */
		if (fan_get_enabled(ch))
			p_status->rpm_actual = fans[ch].rpm_min;
		else
			p_status->rpm_actual = 0;

		p_status->cur_state = TACHO_IN_IDLE;
	}
	return p_status->rpm_actual;
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
	int percent = 0;

	fan_status[ch].rpm_target = rpm;
	/* Transfer rpm to percentage of duty cycle */
	percent = fan_rpm_to_percent(ch, rpm);

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	fan_set_duty(ch, percent);
}

/**
 * Check fan operation status.
 *
 * @param   ch          operation channel
 * @return  fan_status  fan operation status
 */
enum fan_status fan_get_status(int ch)
{
	int rpm_actual;

	rpm_actual = fan_get_rpm_actual(ch);

	if (((fan_get_duty(ch)) && (rpm_actual < fans[ch].rpm_min))
			|| ((!fan_get_duty(ch)) && (rpm_actual)))
		return FAN_STATUS_FRUSTRATED;
	else if ((rpm_actual == 0) && (!fan_get_duty(ch)))
		return FAN_STATUS_STOPPED;
	else
		return FAN_STATUS_LOCKED;
}

/**
 * Check fan is stall condition.
 *
 * @param   ch          operation channel
 * @return  non-zero    if fan is enabled but stalled
 */
int fan_is_stalled(int ch)
{
	int rpm_actual;

	rpm_actual = fan_get_rpm_actual(ch);

	/* Check for normal condition, others are stall condition */
	if ((!fan_get_enabled(ch)) || ((fan_get_duty(ch))
			&& (rpm_actual >= fans[ch].rpm_min))
			|| ((!fan_get_duty(ch)) && (!rpm_actual)))
		return 0;

	return 1;
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
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_INIT_PWM);
