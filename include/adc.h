/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC interface for Chrome EC */

#ifndef __CROS_EC_ADC_H
#define __CROS_EC_ADC_H

#include "adc_chip.h"
#include "common.h"

#define ADC_READ_ERROR -1 /* Value returned by adc_read_channel() on error */

#ifdef CONFIG_ZEPHYR
#include <zephyr_adc.h>
#endif /* CONFIG_ZEPHYR */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ADC
/*
 * Boards must provide this list of ADC channel definitions.  This must match
 * the enum adc_channel list provided by the board.
 */
#ifndef CONFIG_ADC_CHANNELS_RUNTIME_CONFIG
extern const struct adc_t adc_channels[];
#else
extern struct adc_t adc_channels[];
#endif
#endif /* CONFIG_ADC */

/**
 * ADC initial.
 */
#ifdef CONFIG_KEYBOARD_SCAN_ADC
void adc_init(void);
#endif

/*
 * Boards which use the ADC interface must provide enum adc_channel in the
 * board.h file.  See chip/$CHIP/adc_chip.h for additional chip-specific
 * requirements.
 */

/**
 * Read an ADC channel.
 *
 * @param ch		Channel to read
 *
 * @return The scaled ADC value, or ADC_READ_ERROR if error.
 */
int adc_read_channel(enum adc_channel ch);

/**
 * Enable ADC watchdog. Note that interrupts might come in repeatedly very
 * quickly when ADC output goes out of the accepted range.
 *
 * @param ain_id	The AIN to be watched by the watchdog.
 * @param high		The high threshold that the watchdog would trigger
 *			an interrupt when exceeded.
 * @param low		The low threshold.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int adc_enable_watchdog(int ain_id, int high, int low);

/**
 * Disable ADC watchdog.
 *
 * @return		EC_SUCCESS, or non-zero if any error.
 */
int adc_disable_watchdog(void);

/**
 * Set the delay between ADC watchdog samples. This can be used as a trade-off
 * of power consumption and performance.
 *
 * @param delay_ms      The delay in milliseconds between two ADC watchdog
 *                      samples.
 *
 * @return              EC_SUCCESS, or non-zero if any error or not supported.
 */
int adc_set_watchdog_delay(int delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ADC_H */
