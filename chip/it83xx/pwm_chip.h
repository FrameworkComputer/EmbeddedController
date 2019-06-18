/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for IT83xx. */

#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

enum pwm_pcfsr_sel {
	PWM_PRESCALER_C4 = 1,
	PWM_PRESCALER_C6 = 2,
	PWM_PRESCALER_C7 = 3,
};

enum pwm_hw_channel {
	PWM_HW_CH_DCR0 = 0,
	PWM_HW_CH_DCR1,
	PWM_HW_CH_DCR2,
	PWM_HW_CH_DCR3,
	PWM_HW_CH_DCR4,
	PWM_HW_CH_DCR5,
	PWM_HW_CH_DCR6,
	PWM_HW_CH_DCR7,

	PWM_HW_CH_TOTAL,
};

enum tach_ch_sel {
	/* Pin GPIOD.6 */
	TACH_CH_TACH0A = 0,
	/* Pin GPIOD.7 */
	TACH_CH_TACH1A,
	/* Pin GPIOJ.2 */
	TACH_CH_TACH0B,
	/* Pin GPIOJ.3 */
	TACH_CH_TACH1B,
	/* Number of TACH channels */
	TACH_CH_COUNT,

	TACH_CH_NULL = 0xFF,
};

/* Data structure to define PWM channel control registers. */
struct pwm_ctrl_t {
	/* PWM channel output duty register. */
	volatile uint8_t *pwm_duty;
	/* PWM channel clock source selection register. */
	volatile uint8_t *pwm_clock_source;
	/* PWM channel pin control register. */
	volatile uint8_t *pwm_pin;
};

/* Data structure to define PWM channel control registers part 2. */
struct pwm_ctrl_t2 {
	/* PWM cycle time register. */
	volatile uint8_t *pwm_cycle_time;
	/* PWM channel clock prescaler register (LSB). */
	volatile uint8_t *pwm_cpr_lsb;
	/* PWM channel clock prescaler register (MSB). */
	volatile uint8_t *pwm_cpr_msb;
	/* PWM prescaler clock frequency select register. */
	volatile uint8_t *pwm_pcfsr_reg;
	/* PWM prescaler clock frequency select register setting. */
	uint8_t pwm_pcfsr_ctrl;
};

/* Data structure to define PWM channels. */
struct pwm_t {
	/* PWM channel ID */
	int channel;
	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
	int freq_hz;
	enum pwm_pcfsr_sel pcfsr_sel;
};

/* Tachometer channel of each physical fan */
struct fan_tach_t {
	enum tach_ch_sel ch_tach;
	/* the numbers of square pulses per revolution of fan. */
	int fan_p;
	/* allow actual rpm ~= targe rpm +- rpm_re */
	int rpm_re;
	/* startup duty of fan */
	int s_duty;
};

extern const struct pwm_t pwm_channels[];
/* The list of tachometer channel of fans is instantiated in board.c. */
extern const struct fan_tach_t fan_tach[];

void pwm_duty_inc(enum pwm_channel ch);
void pwm_duty_reduce(enum pwm_channel ch);

#endif /* __CROS_EC_PWM_CHIP_H */
