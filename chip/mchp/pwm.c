/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for MCHP MEC family */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "util.h"
#include "tfdp_chip.h"

#define CPUTS(outstr) cputs(CC_PWM, outstr)
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

/*
 * PWMs that must remain active in low-power idle -
 * PWM 0,1-8 are b[4,20:27] of MCHP_PCR_SLP_EN1
 * PWM 9 is b[31] of MCHP_PCR_SLP_EN3
 * PWM 10 - 11 are b[0:1] of MCHP_PCR_SLP_EN4
 * store 32-bit word with
 * b[0:1] = PWM 10-11
 * b[4,20:27] = PWM 0, 1-8
 * b[31] = PWM 9
 */
static uint32_t pwm_keep_awake_mask;
static uint32_t bbled_keep_awake_mask;


const uint8_t pwm_slp_bitpos[MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES] = {
	4, 20, 21, 22, 23, 24, 25, 26, 27,
#if defined(CHIP_FAMILY_MEC17XX)
	31, 0, 1,
#endif
	/* BBLED instances*/
	16, 17, 18,
	#if defined(CHIP_FAMILY_MEC17XX)
	25
	#endif

};

void bbled_set_limit(enum pwm_channel ch, uint8_t max, uint8_t min)
{
	MCHP_BBLED_LIMITS(ch) = (max << 8) + min;
}

/*
 * In 8-bit mode 1 cycle = 8ms.
 * high byte for light on, low byte for light off
 */
void bbled_set_delay(enum pwm_channel ch, int high_delay, int low_delay)
{
	MCHP_BBLED_DELAY(ch) = (high_delay << MCHP_BBLED_DLY_HI_BITPOS) + low_delay;
}

void bbled_enable(enum pwm_channel ch, int percent, int on_length, int off_length, uint8_t enable)
{
	int id = pwm_channels[ch].channel;
	int duty = (percent * 0xFF) / 100;

	if (id < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		id = id - MCHP_PWM_ID_MAX;
		if (enable) {
			if (!(MCHP_BBLED_CONFIG(id) & MCHP_BBLED_CTRL_BREATHE)) {
				MCHP_BBLED_CONFIG(id) &= ~MCHP_BBLED_CTRL_MASK;
				MCHP_BBLED_CONFIG(id) &= ~MCHP_BBLED_ASYMMETRIC;
				MCHP_BBLED_CONFIG(id) |= MCHP_BBLED_CTRL_BREATHE;
				MCHP_BBLED_CONFIG(id) |= MCHP_BBLED_SYNC;
				bbled_set_limit(id, duty, 0x00);
				bbled_set_delay(id, on_length, off_length);
				MCHP_BBLED_CONFIG(id) &= ~MCHP_BBLED_SYNC;
				MCHP_BBLED_CONFIG(id) |= MCHP_BBLED_EN_UPDATE;
			}
		} else {
			if (!(MCHP_BBLED_CONFIG(id) & MCHP_BBLED_CTRL_BLINK)) {
				MCHP_BBLED_CONFIG(id) &= ~MCHP_BBLED_CTRL_MASK;
				MCHP_BBLED_CONFIG(id) |= MCHP_BBLED_CTRL_BLINK;
				bbled_set_delay(id, 0x00, 0x0f);
			}
		}
	}
}

static uint32_t pwm_get_sleep_mask(int id)
{
	uint32_t bitpos = 32;

	if (id < MCHP_PWM_ID_MAX)
		bitpos = (uint32_t)pwm_slp_bitpos[id];

	return (1ul << bitpos);
}

static uint32_t pwm_get_bb_sleep_mask(int id)
{
	uint32_t bitpos = 32;

	if (id >= MCHP_PWM_ID_MAX && id < MCHP_PWM_ID_MAX)
		bitpos = (uint32_t)pwm_slp_bitpos[id];
	return (1ul << bitpos);
}


void pwm_enable(enum pwm_channel ch, int enabled)
{
	int id = pwm_channels[ch].channel;
	uint32_t pwm_slp_mask;

	if (id < MCHP_PWM_ID_MAX) {
		pwm_slp_mask = pwm_get_sleep_mask(id);

		if (enabled) {
			MCHP_PWM_CFG(id) |= 0x1;
			if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
				pwm_keep_awake_mask |= pwm_slp_mask;
		} else {
			MCHP_PWM_CFG(id) &= ~0x1;
			pwm_keep_awake_mask &= ~pwm_slp_mask;
		}
	} else if (id < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		pwm_slp_mask = pwm_get_bb_sleep_mask(id);
		id = id - MCHP_PWM_ID_MAX;
		/* Handle BBLEDs here*/
		if (enabled) {
			/* Blink = PWM mode when clock=high*/
			MCHP_BBLED_CONFIG(id) |= MCHP_BBLED_CTRL_BLINK;
			if (pwm_channels[ch].flags & PWM_CONFIG_DSLEEP)
				bbled_keep_awake_mask |= pwm_slp_mask;
		} else {
			MCHP_BBLED_CONFIG(id) &= ~MCHP_BBLED_CTRL_MASK;
			bbled_keep_awake_mask &= ~pwm_slp_mask;
		}
	}
}

int pwm_get_enabled(enum pwm_channel ch)
{
	int id = pwm_channels[ch].channel;

	if (id < MCHP_PWM_ID_MAX) {
		return MCHP_PWM_CFG(id) & 0x1;
	} else if (id < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		id = id - MCHP_PWM_ID_MAX;
		return (MCHP_BBLED_CONFIG(id) & MCHP_BBLED_CTRL_MASK) != MCHP_BBLED_CTRL_OFF;
	}
	return 0;
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	int id = pwm_channels[ch].channel;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;
	if (id < MCHP_PWM_ID_MAX) {
		MCHP_PWM_ON(id) = percent;
		MCHP_PWM_OFF(id) = 100 - percent;
	} else if (id < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		id = id - MCHP_PWM_ID_MAX;
		/* The BBLED peripheral does not have the ability to set reload value*/
		MCHP_BBLED_LIMIT_MIN(id) = (percent * 0xFF) / 100;
	}
}

int pwm_get_duty(enum pwm_channel ch)
{
	int id = pwm_channels[ch].channel;

	if (id < MCHP_PWM_ID_MAX) {
		return MCHP_PWM_ON(pwm_channels[ch].channel);
	} else if (id < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		id = id - MCHP_PWM_ID_MAX;
		return DIV_ROUND_NEAREST(MCHP_BBLED_LIMIT_MIN(id) * 100, 0xFF);
	}
	return -1;
}

void pwm_keep_awake(void)
{
	if (pwm_keep_awake_mask) {
		/* b[4,20:27] */
		MCHP_PCR_SLP_EN1 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN1_PWM_ALL));
		/* b[31] */
		MCHP_PCR_SLP_EN3 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN3_PWM_ALL));
		/* b[1:0] */
		MCHP_PCR_SLP_EN4 &= ~(pwm_keep_awake_mask &
					 (MCHP_PCR_SLP_EN4_PWM_ALL));
	} else {
		/*Disable 100khz clock - this is shared with tach*/
		MCHP_PCR_SLOW_CLK_CTL &= ~MCHP_PCR_SLOW_CLK_CTL_MASK;
	}
	if (bbled_keep_awake_mask) {
		MCHP_PCR_SLP_EN3 &= ~(bbled_keep_awake_mask &
					(MCHP_PCR_SLP_EN3_LED_ALL));
	}
}


void pwm_configure(int ch, int active_low, int clock_low)
{
	/*
	 * clock_low=0 selects the 48MHz Ring Oscillator source
	 * clock_low=1 selects the 100kHz_Clk source
	 */
	if (ch < MCHP_PWM_ID_MAX) {
		MCHP_PWM_CFG(ch) = (15 << 3) |    /* Pre-divider = 16 */
			      (active_low ? BIT(2) : 0) |
			      (clock_low  ? BIT(1) : 0);
	} else if (ch < (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)) {
		ch = ch - MCHP_PWM_ID_MAX;
		/** Clock low selects the 32.768 clock,
		 * and Clock high selects the main system clock
		 */
		MCHP_BBLED_CONFIG(ch) = (clock_low ? 0 : MCHP_BBLED_CLK_48M) |
								MCHP_BBLED_ASYMMETRIC;

		/** in PWM mode the delay register sets the prescaler:
		 * Fpwm = Fclock/(256*(DELAY+1))
		 * with Fclock set to 48Mhz:
		 * 8 =  20833Hz
		 * 15 = 11718Hz
		 * Lets set it above audio frequencies
		 */
		MCHP_BBLED_DELAY(ch) = 15;
	}
}

static const uint16_t pwm_pcr[MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES] = {
	MCHP_PCR_PWM0, /*Located in Sleep enable 1*/
	MCHP_PCR_PWM1,
	MCHP_PCR_PWM2,
	MCHP_PCR_PWM3,
	MCHP_PCR_PWM4,
	MCHP_PCR_PWM5,
	MCHP_PCR_PWM6,
	MCHP_PCR_PWM7,
	MCHP_PCR_PWM8,
#if defined(CHIP_FAMILY_MEC17XX)
	MCHP_PCR_PWM9, /*Located in Sleep enable 3*/
	MCHP_PCR_PWM10,/*Located in Sleep enable 4*/
	MCHP_PCR_PWM11,/*Located in Sleep enable 4*/
#endif
	MCHP_PCR_LED0, /*Located in Sleep enable 3*/
	MCHP_PCR_LED1, /*Located in Sleep enable 3*/
	MCHP_PCR_LED2, /*Located in Sleep enable 3*/
#if defined(CHIP_FAMILY_MEC17XX)
	MCHP_PCR_LED3, /*Located in Sleep enable 3*/
#endif
};

void pwm_slp_en(int pwm_id, int sleep_en)
{
	if ((pwm_id < 0) || (pwm_id > (MCHP_PWM_ID_MAX + MCHP_BBLEN_INSTANCES)))
		return;

	if (sleep_en)
		MCHP_PCR_SLP_EN_DEV(pwm_pcr[pwm_id]);
	else
		MCHP_PCR_SLP_DIS_DEV(pwm_pcr[pwm_id]);
}

static void pwm_init(void)
{
	int i;

	for (i = 0; i < PWM_CH_COUNT; ++i) {
		pwm_slp_en(pwm_channels[i].channel, 0);
		pwm_configure(pwm_channels[i].channel,
			      pwm_channels[i].flags & PWM_CONFIG_ACTIVE_LOW,
			      pwm_channels[i].flags & PWM_CONFIG_ALT_CLOCK);
		pwm_set_duty(i, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, pwm_init, HOOK_PRIO_DEFAULT);
