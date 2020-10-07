/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT83xx ADC module for Chrome EC */

#ifndef __CROS_EC_ADC_CHIP_H
#define __CROS_EC_ADC_CHIP_H

#include "common.h"

/*
 * Maximum time we allow for an ADC conversion.
 * NOTE:
 * Because this setting greater than "SLEEP_SET_HTIMER_DELAY_USEC" in clock.c,
 * so we enabled sleep mask to prevent going in to deep sleep while ADC
 * converting.
 */
#define ADC_TIMEOUT_US MSEC

/* Minimum and maximum values returned by adc_read_channel(). */
#define ADC_READ_MIN 0
#define ADC_READ_MAX 1023
#define ADC_MAX_MVOLT 3000

/* List of ADC channels. */
enum chip_adc_channel {
	CHIP_ADC_CH0 = 0,
	CHIP_ADC_CH1,
	CHIP_ADC_CH2,
	CHIP_ADC_CH3,
	CHIP_ADC_CH4,
	CHIP_ADC_CH5,
	CHIP_ADC_CH6,
	CHIP_ADC_CH7,
	CHIP_ADC_CH13,
	CHIP_ADC_CH14,
	CHIP_ADC_CH15,
	CHIP_ADC_CH16,
	CHIP_ADC_COUNT,
};

/* List of voltage comparator. */
enum chip_vcmp {
	CHIP_VCMP0 = 0,
	CHIP_VCMP1,
	CHIP_VCMP2,
	CHIP_VCMP3,
	CHIP_VCMP4,
	CHIP_VCMP5,
	CHIP_VCMP_COUNT,
};

/* List of voltage comparator scan period times. */
enum vcmp_scan_period {
	VCMP_SCAN_PERIOD_100US = 0x10,
	VCMP_SCAN_PERIOD_200US = 0x20,
	VCMP_SCAN_PERIOD_400US = 0x30,
	VCMP_SCAN_PERIOD_600US = 0x40,
	VCMP_SCAN_PERIOD_800US = 0x50,
	VCMP_SCAN_PERIOD_1MS   = 0x60,
	VCMP_SCAN_PERIOD_1_5MS = 0x70,
	VCMP_SCAN_PERIOD_2MS   = 0x80,
	VCMP_SCAN_PERIOD_2_5MS = 0x90,
	VCMP_SCAN_PERIOD_3MS   = 0xA0,
	VCMP_SCAN_PERIOD_4MS   = 0xB0,
	VCMP_SCAN_PERIOD_5MS   = 0xC0,
};

/* Data structure to define ADC channel control registers. */
struct adc_ctrl_t {
	volatile uint8_t *adc_ctrl;
	volatile uint8_t *adc_datm;
	volatile uint8_t *adc_datl;
};

/* Data structure to define ADC channels. */
struct adc_t {
	const char *name;
	int factor_mul;
	int factor_div;
	int shift;
	enum chip_adc_channel channel;
};

/* Data structure to define voltage comparator control registers. */
struct vcmp_ctrl_t {
	volatile uint8_t *vcmp_ctrl;
	volatile uint8_t *vcmp_adc_chm;
	volatile uint8_t *vcmp_datm;
	volatile uint8_t *vcmp_datl;
};

/* supported flags (member "flag" in struct vcmp_t) for voltage comparator */
#define GREATER_THRESHOLD         BIT(0)
#define LESS_EQUAL_THRESHOLD      BIT(1)

/* Data structure for board to define voltage comparator list. */
struct vcmp_t {
	const char *name;
	int threshold;
	/*
	 * Select greater/less equal threshold.
	 * NOTE: once edge trigger interrupt fires, we need disable the voltage
	 *       comparator, or the matching threshold level will infinitely
	 *       triggers interrupt.
	 */
	char flag;
	/* Called when the interrupt fires */
	void (*vcmp_thresh_cb)(void);
	/*
	 * Select "all voltage comparator" scan period time.
	 * The power consumption is positively relative with scan frequency.
	 */
	enum vcmp_scan_period scan_period;
	/*
	 * Select which ADC channel output voltage into comparator and we
	 * should set the ADC channel pin in alternate mode via adc_channels[].
	 */
	enum chip_adc_channel adc_ch;
};

/*
 * Boards must provide this list of ADC channel definitions. This must match
 * the enum adc_channel list provided by the board.
 */
extern const struct adc_t adc_channels[];

#ifdef CONFIG_ADC_VOLTAGE_COMPARATOR
/*
 * Boards must provide this list of voltage comparator definitions.
 * This must match the enum board_vcmp list provided by the board.
 */
extern const struct vcmp_t vcmp_list[];
#endif
void vcmp_enable(int index, int enable);

#endif /* __CROS_EC_ADC_CHIP_H */
