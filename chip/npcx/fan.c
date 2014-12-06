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

/* Fan operation module */
enum npcx_fan_op_module {
	NPCX_FAN_OP_PWM,
	NPCX_FAN_OP_MFT,
	/* Number of FAN module operations */
	NPCX_FAN_OP_COUNT
};

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

/* MFT clock source */
enum npcx_mft_clk_src {
	TCKC_NOCLK = 0,
	TCKC_PRESCALE_APB1_CLK,
	TCKC_EXTERNAL,
	TCKC_PULSE_ACC,
	TCKC_LFCLK
};

#define RPM_SCALE                   1 /* Fan RPM is multiplier of actual RPM */
#define RPM_EDGES                   1 /* Fan number of edges - 1 */
/*
 * RPM = (n - 1) * m * f * 60 / poles / TACH
 *   n = Fan number of edges = (RPM_EDGES + 1)
 *   m = Fan multiplier defined by RANGE
 *   f = PWM and MFT freq
 *   poles = 2
 */
#define RPM_TO_TACH(pwm_channel, rpm) \
	MIN(((uint32_t)(pwm_channels[pwm_channel].freq)*30*RPM_EDGES*RPM_SCALE \
	/MAX((rpm), 1)), (pwm_channels[pwm_channel].cycle_pulses))

#define TACH_TO_RPM(mft_channel, tach) \
	((mft_channels[mft_channel].freq)*30*RPM_EDGES*RPM_SCALE \
	/MAX((tach), 1))

/* Global variables */
static volatile struct tacho_status_t tacho_status;
static int rpm_target;
static int rpm_actual = -1;
static int fan_init_ch;
/**
 * Select fan operation channel by module.
 *
 * @param   none
 * @param   op_module   npcx operation module
 * @return  npcx operation channel by module
 * @notes   Fan is controlled by PWM/MFT module in npcx chip
 */
static int fan_op_ch(int ch, enum npcx_fan_op_module op_module)
{
	uint8_t op_ch;

	switch (ch) {
	case 0:
		if (op_module == NPCX_FAN_OP_PWM)
			op_ch = PWM_CH_FAN;
		else
			op_ch = MFT_CH_0;
		break;
	default:
		op_ch = 0;
		break;
	}
	return op_ch;
}

/**
 * MFT start measure.
 *
 * @param   ch      operation channel
 * @return  none
 */
static void mft_startmeasure(int ch)
{
	int mft_ch = fan_op_ch(ch, NPCX_FAN_OP_MFT);

	/* Start measurement */
#ifdef CONFIG_MFT_INPUT_LFCLK
	/* Set the LFCLK clock. */
	if (NPCX_MFT_MODULE_PORT_TB == mft_channels[mft_ch].port)
		NPCX_TCKC(mft_channels[mft_ch].module) =
			(NPCX_TCKC(mft_channels[mft_ch].module)
			&(~(((1<<3)-1)<<NPCX_TCKC_C2CSEL)))
			|(TCKC_LFCLK<<NPCX_TCKC_C2CSEL);
	else
		NPCX_TCKC(mft_channels[mft_ch].module) =
			(NPCX_TCKC(mft_channels[mft_ch].module)
			&(~(((1<<3)-1)<<NPCX_TCKC_C1CSEL)))
			|(TCKC_LFCLK<<NPCX_TCKC_C1CSEL);
#else
	/* Set the core clock. */
	if (NPCX_MFT_MODULE_PORT_TB == mft_channels[mft_ch].port)
		NPCX_TCKC(mft_channels[mft_ch].module) =
			(NPCX_TCKC(mft_channels[mft_ch].module)
			&(~(((1<<3)-1)<<NPCX_TCKC_C2CSEL)))
			|(TCKC_PRESCALE_APB1_CLK<<NPCX_TCKC_C2CSEL);
	else
		NPCX_TCKC(mft_channels[mft_ch].module) =
			(NPCX_TCKC(mft_channels[mft_ch].module)
			&(~(((1<<3)-1)<<NPCX_TCKC_C1CSEL)))
			|(TCKC_PRESCALE_APB1_CLK<<NPCX_TCKC_C1CSEL);
#endif
}

/**
 * MFT stop measure.
 *
 * @param   ch      operation channel
 * @return  none
 */
static void mft_stopmeasure(int ch)
{
	int mft_ch = fan_op_ch(ch, NPCX_FAN_OP_MFT);

	/* Clear all pending flag */
	NPCX_TECLR(mft_channels[mft_ch].module) =
			NPCX_TECTRL(mft_channels[mft_ch].module);

	/* Stop the timer */
	if (NPCX_MFT_MODULE_PORT_TB == mft_channels[mft_ch].port)
		NPCX_TCKC(mft_channels[mft_ch].module) =
				(NPCX_TCKC(mft_channels[mft_ch].module)
				&(~(((1<<3)-1)<<NPCX_TCKC_C2CSEL)))
				|(TCKC_NOCLK<<NPCX_TCKC_C2CSEL);
	else
		NPCX_TCKC(mft_channels[mft_ch].module) =
				(NPCX_TCKC(mft_channels[mft_ch].module)
				&(~(((1<<3)-1)<<NPCX_TCKC_C1CSEL)))
				|(TCKC_NOCLK<<NPCX_TCKC_C1CSEL);
}

/**
 * MFT final measure.
 *
 * @param   ch      operation channel
 * @return  none
 */
static void mft_finalmeasure(int ch)
{
	int mft_ch = fan_op_ch(ch, NPCX_FAN_OP_MFT);

	/*
	 * Start of the last tacho cycle is detected -
	 * calculated tacho cycle duration
	 */
	if (NPCX_MFT_MODULE_PORT_TB == mft_channels[mft_ch].port)
		tacho_status.edge_interval =
				(uint32_t)(mft_channels[mft_ch].default_count
				- NPCX_TCRB(mft_channels[mft_ch].module));
	else
		tacho_status.edge_interval =
				(uint32_t)(mft_channels[mft_ch].default_count
				- NPCX_TCRA(mft_channels[mft_ch].module));
}

/**
 * Preset fan operation clock.
 *
 * @param   none
 * @return  none
 * @notes   changed when initial or HOOK_FREQ_CHANGE command
 */
#ifndef CONFIG_MFT_INPUT_LFCLK
void mft_freq_changed(void)
{
	uint16_t prescaler_divider    = 0;
	int mft_ch = fan_op_ch(fan_init_ch, NPCX_FAN_OP_MFT);

	/* Set clock prescaler divider to MFT module*/
	prescaler_divider = (uint16_t)(clock_get_apb1_freq()
			/mft_channels[mft_ch].freq);
	if (prescaler_divider >= 1)
		prescaler_divider = prescaler_divider - 1;
	if (prescaler_divider > 0xFF)
		prescaler_divider = 0xFF;

	NPCX_TPRSC(mft_channels[mft_ch].module) = (uint8_t)prescaler_divider;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, mft_freq_changed, HOOK_PRIO_DEFAULT);
#endif

/**
 * Fan configuration.
 *
 * @param ch                        operation channel
 * @param enable_mft_read_rpm       FAN_USE_RPM_MODE enable flag
 * @return none
 */
static void fan_config(int ch, int enable_mft_read_rpm)
{
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);
	int mft_ch = fan_op_ch(ch, NPCX_FAN_OP_MFT);

	fan_init_ch = ch;
	pwm_config(pwm_ch);

	/* Mux mft */
	CLEAR_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TB1_TACH2_SL1);
	/* Configure pins from GPIOs to FAN */
	gpio_config_module(MODULE_PWM_FAN, 1);

	if (enable_mft_read_rpm) {
		/* Set mode 5 to MFT module*/
		NPCX_TMCTRL(mft_channels[mft_ch].module) =
				(NPCX_TMCTRL(mft_channels[mft_ch].module)
				& (~(((1<<3)-1)<<NPCX_TMCTRL_MDSEL)))
				| (NPCX_MFT_MDSEL_5<<NPCX_TMCTRL_MDSEL);

#ifndef CONFIG_MFT_INPUT_LFCLK
		/* Set MFT operation frequence */
		mft_freq_changed();
		/* Set the active power mode. */
		CLEAR_BIT(NPCX_TCKC(mft_channels[mft_ch].module),
				NPCX_TCKC_LOW_PWR);
#else
		/* Set the low power mode. */
		SET_BIT(NPCX_TCKC(mft_channels[mft_ch].module),
				NPCX_TCKC_LOW_PWR);
#endif
		if (NPCX_MFT_MODULE_PORT_TB == mft_channels[mft_ch].port) {
			/* Set the default count-down timer. */
			NPCX_TCNT2(mft_channels[mft_ch].module) =
					mft_channels[mft_ch].default_count;
			NPCX_TCRB(mft_channels[mft_ch].module) =
					mft_channels[mft_ch].default_count;
			/* Set the edge polarity to rising. */
			SET_BIT(NPCX_TMCTRL(mft_channels[mft_ch].module),
					NPCX_TMCTRL_TBEDG);
			/* Enable capture TCNT2 into TCRB and preset TCNT2. */
			SET_BIT(NPCX_TMCTRL(mft_channels[mft_ch].module),
					NPCX_TMCTRL_TBEN);
			/* Enable input debounce logic into TB. */
			SET_BIT(NPCX_TCFG(mft_channels[mft_ch].module),
					NPCX_TCFG_TBDBEN);
			/* Set the no clock to TCNT2. */
			NPCX_TCKC(mft_channels[mft_ch].module) =
					(NPCX_TCKC(mft_channels[mft_ch].module)
					& (~(((1<<3)-1)<<NPCX_TCKC_C2CSEL)))
					| (TCKC_NOCLK<<NPCX_TCKC_C2CSEL);
			/* Set timer wake-up enable */
			SET_BIT(NPCX_TWUEN(mft_channels[mft_ch].module),
					NPCX_TWUEN_TBWEN);
			SET_BIT(NPCX_TWUEN(mft_channels[mft_ch].module),
					NPCX_TWUEN_TDWEN);
		} else {
			/* Set the default count-down timer. */
			NPCX_TCNT1(mft_channels[mft_ch].module) =
					mft_channels[mft_ch].default_count;
			NPCX_TCRA(mft_channels[mft_ch].module) =
					mft_channels[mft_ch].default_count;
			/* Set the edge polarity to rising. */
			SET_BIT(NPCX_TMCTRL(mft_channels[mft_ch].module),
					NPCX_TMCTRL_TAEDG);
			/* Enable capture TCNT1 into TCRA and preset TCNT1. */
			SET_BIT(NPCX_TMCTRL(mft_channels[mft_ch].module),
					NPCX_TMCTRL_TAEN);
			/* Enable input debounce logic into TA. */
			SET_BIT(NPCX_TCFG(mft_channels[mft_ch].module),
					NPCX_TCFG_TADBEN);
			/* Set the no clock to TCNT1. */
			NPCX_TCKC(mft_channels[mft_ch].module) =
				  (NPCX_TCKC(mft_channels[mft_ch].module)
				& (~(((1<<3)-1)<<NPCX_TCKC_C1CSEL)))
				| (TCKC_NOCLK<<NPCX_TCKC_C1CSEL);
			/* Set timer wake-up enable */
			SET_BIT(NPCX_TWUEN(mft_channels[mft_ch].module),
					NPCX_TWUEN_TAWEN);
			SET_BIT(NPCX_TWUEN(mft_channels[mft_ch].module),
					NPCX_TWUEN_TCWEN);
		}
	}

	/* Back to Idle mode*/
	tacho_status.cur_state = TACHO_IN_IDLE;
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
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);

	pwm_enable(pwm_ch, enabled);
}

/**
 * Check fan enabled.
 *
 * @param   ch  operation channel
 * @return  enabled or not
 */
int fan_get_enabled(int ch)
{
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);

	return pwm_get_enabled(pwm_ch);
}

/**
 * Set fan duty cycle.
 *
 * @param   ch      operation channel
 * @param   percent duty cycle percent
 * @return  none
 */
void fan_set_duty(int ch, int percent)
{
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);

	CPRINTS("set duty percent=%d", percent);

	/* Set the duty cycle */
	pwm_set_duty(pwm_ch, percent);
}

/**
 * Get fan duty cycle.
 *
 * @param   ch  operation channel
 * @return  duty cycle
 */
int fan_get_duty(int ch)
{
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);

	/* Return percent */
	return pwm_get_duty(pwm_ch);
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
	int mft_ch = fan_op_ch(ch, NPCX_FAN_OP_MFT);
	uint8_t capture_pnd = NPCX_TECTRL_TBPND,
		underflow_pnd = NPCX_TECTRL_TDPND;
	uint8_t capture_clr = NPCX_TECLR_TBCLR,
		underflow_clr = NPCX_TECLR_TDCLR;

	/* Init pending/clear flag bit */
	if (NPCX_MFT_MODULE_PORT_TA == mft_channels[mft_ch].port) {
		capture_pnd = NPCX_TECTRL_TAPND;
		underflow_pnd = NPCX_TECTRL_TCPND;
		capture_clr = NPCX_TECLR_TACLR;
		underflow_clr = NPCX_TECLR_TCCLR;
	}
	/* Start measure and return previous value when fan is working*/
	if ((fan_get_enabled(ch)) && (fan_get_duty(ch))) {
		if (tacho_status.cur_state == TACHO_IN_IDLE) {
			CPRINTS("mft_startmeasure");
			if ((0 == rpm_actual) || (-1 == rpm_actual))
				rpm_actual = fans[ch].rpm_min;
			/* Clear all pending flags */
			NPCX_TECLR(mft_channels[mft_ch].module) =
				   NPCX_TECTRL(mft_channels[mft_ch].module);
			/* Start from first edge state */
			tacho_status.cur_state = TACHO_WAIT_FOR_1_EDGE;
			/* Start measure */
			mft_startmeasure(ch);
		}
		/* Check whether MFT underflow flag is occurred */
		else if (IS_BIT_SET(NPCX_TECTRL(mft_channels[mft_ch].module),
				underflow_pnd)) {
			/* Measurement is active - stop the measurement */
			mft_stopmeasure(fan_init_ch);
			/* Need to avoid underflow state happen */
			rpm_actual = fans[ch].rpm_max;
			/*
			 * Flag TDPND means mft underflow happen then complete
			 * measurement immediately
			 */
			tacho_status.cur_state = TACHO_UNDERFLOW;
			CPRINTS("TACHO_UNDERFLOW");

			/* Clear pending flags */
			SET_BIT(NPCX_TECLR(mft_channels[mft_ch].module),
					underflow_clr);
		}
		/* Check whether MFT signal detection flag is occurred */
		else if (IS_BIT_SET(NPCX_TECTRL(mft_channels[mft_ch].module),
				capture_pnd)) {
			/* Start of tacho cycle is detected */
			switch (tacho_status.cur_state) {
			case TACHO_WAIT_FOR_1_EDGE:
				CPRINTS("TACHO_WAIT_FOR_1_EDGE");
				/*
				 * Start of the first tacho cycle is detected
				 * and wait for the second tacho cycle
				 * (second edge)
				 */
				tacho_status.cur_state = TACHO_WAIT_FOR_2_EDGE;
				/* Send previous rpm before complete measure */
				break;
			case TACHO_WAIT_FOR_2_EDGE:
				/* Complete measure tach and get actual tach */
				mft_finalmeasure(fan_init_ch);
				/* Stop the measurement */
				mft_stopmeasure(ch);
				/* Transfer actual tach to actual rpm */
				rpm_actual = (tacho_status.edge_interval > 0) ?
					     (TACH_TO_RPM(mft_ch,
					     tacho_status.edge_interval)) : 0;
				/* Back to Idle mode*/
				tacho_status.cur_state = TACHO_IN_IDLE;
				CPRINTS("TACHO_WAIT_FOR_2_EDGE");
				CPRINTS("edge_interval=%x",
						tacho_status.edge_interval);
				CPRINTS("rpm_actual=%d", rpm_actual);
				break;
			default:
				break;
			}
			/* Clear pending flags */
			SET_BIT(NPCX_TECLR(mft_channels[mft_ch].module),
						capture_clr);
		}
	} else {
		CPRINTS("preset rpm");
		/* Send preset rpm before fan is working */
		if (fan_get_enabled(ch))
			rpm_actual = fans[ch].rpm_min;
		else
			rpm_actual = 0;

		tacho_status.cur_state = TACHO_IN_IDLE;
	}
	return rpm_actual;
}

/**
 * Get fan setting rpm.
 *
 * @param   ch  operation channel
 * @return  setting rpm value
 */
int fan_get_rpm_target(int ch)
{
	return rpm_target;
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
	uint32_t percent = 0;
	int pwm_ch = fan_op_ch(ch, NPCX_FAN_OP_PWM);

	rpm_target = rpm;
	/* Transfer rpm to tach then calculate percentage */
	percent = (RPM_TO_TACH(pwm_ch, rpm_target)*100)
		/(pwm_channels[pwm_ch].cycle_pulses);

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	/* RPM is inverse ratio to tach and percentage */
	percent = 100 - percent;

	pwm_set_duty(pwm_ch, percent);
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

	if (((fan_get_duty(ch)) && (0 == rpm_actual))
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
	if ((!fan_get_enabled(ch)) || ((fan_get_duty(ch)) && (rpm_actual))
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
#ifdef CONFIG_PWM_DSLEEP
	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, CGC_FAN_MASK, CGC_MODE_ALL);
#else
	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, CGC_FAN_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);
#endif
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_INIT_PWM);
