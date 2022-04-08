/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_PWRSEQ_SIGNAL_ADC_H__
#define __AP_PWRSEQ_SIGNAL_ADC_H__

#define PWR_SIG_TAG_ADC	PWR_ADC_

/*
 * Generate enums for the analogue converters.
 * These enums are only used internally
 * to assign an index to each signal that is specific
 * to the source.
 */

#define TAG_ADC(tag, name) DT_CAT(tag, name)

#define PWR_ADC_ENUM(id) TAG_ADC(PWR_SIG_TAG_ADC, PWR_SIGNAL_ENUM(id)),

enum pwr_sig_adc {
#if HAS_ADC_SIGNALS
DT_FOREACH_STATUS_OKAY(intel_ap_pwrseq_adc, PWR_ADC_ENUM)
#endif
	PWR_SIG_ADC_COUNT
};

#undef	PWR_ADC_ENUM
#undef	TAG_ADC

/**
 * @brief Get the value of the ADC power signal.
 *
 * The analogue-to-digital converter is used to read an analogue
 * value and to convert that via a threshold to a 0/1 signal.
 *
 * @param adc The enum of the value to get.
 * @return the current value of the power signal.
 */
int power_signal_adc_get(enum pwr_sig_adc adc);

/**
 * @brief Enable the ADC interrupt
 *
 * This will not only enable the interrupt driven update
 * of this signal, but will also enable the ADC itself.
 *
 * @param signal The pwr_sig_adc to enable.
 * @return 0 if successful
 * @return -error if failed
 */
int power_signal_adc_enable_int(enum pwr_sig_adc adc);

/**
 * @brief Disable the ADC interrupt
 *
 * This will disable the interrupt updating of this signal, and will
 * also disable the ADC from running.
 *
 * @param signal The pwr_sig_adc to disable.
 * @return 0 if successful
 * @return -error if failed
 */
int power_signal_adc_disable_int(enum pwr_sig_adc adc);

/**
 * @brief Initialize the ADCs for the power signals.
 */
void power_signal_adc_init(void);

#endif /* __AP_PWRSEQ_SIGNAL_ADC_H__ */
