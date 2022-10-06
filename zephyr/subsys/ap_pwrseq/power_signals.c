/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <ap_power/ap_pwrseq.h>
#include <power_signals.h>

#include "signal_gpio.h"
#include "signal_vw.h"
#include "signal_adc.h"

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(intel_ap_pwrseq)
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(intel_ap_pwrseq) == 1,
	     "Only one node for intel_ap_pwrseq is allowed");
#endif

BUILD_ASSERT(POWER_SIGNAL_COUNT <= 32, "Too many power signals");

/*
 * Enum indicating type of signal.
 */
enum signal_source {
	PWR_SIG_SRC_GPIO,
	PWR_SIG_SRC_VW,
	PWR_SIG_SRC_EXT,
	PWR_SIG_SRC_ADC,
};

struct ps_config {
	const char *debug_name;
	uint8_t source;
	uint8_t src_enum;
};

#define TAG_PWR_ENUM(tag, name) DT_CAT(tag, name)

#define PWR_ENUM(id, tag) TAG_PWR_ENUM(tag, PWR_SIGNAL_ENUM(id))

#define DBGNAME(id) "(" DT_PROP(id, enum_name) ") " DT_PROP(id, dbg_label)

#define GEN_PS_ENTRY(id, src, tag)             \
	{                                      \
		.debug_name = DBGNAME(id),     \
		.source = src,                 \
		.src_enum = PWR_ENUM(id, tag), \
	},

#define GEN_PS_ENTRY_NO_ENUM(id, src)      \
	{                                  \
		.debug_name = DBGNAME(id), \
		.source = src,             \
	},

/*
 * Generate the power signal configuration array.
 */
static const struct ps_config sig_config[] = {
	DT_FOREACH_STATUS_OKAY_VARGS(intel_ap_pwrseq_gpio, GEN_PS_ENTRY,
				     PWR_SIG_SRC_GPIO, PWR_SIG_TAG_GPIO)
		DT_FOREACH_STATUS_OKAY_VARGS(intel_ap_pwrseq_vw, GEN_PS_ENTRY,
					     PWR_SIG_SRC_VW, PWR_SIG_TAG_VW)
			DT_FOREACH_STATUS_OKAY_VARGS(intel_ap_pwrseq_external,
						     GEN_PS_ENTRY_NO_ENUM,
						     PWR_SIG_SRC_EXT)
				DT_FOREACH_STATUS_OKAY_VARGS(
					intel_ap_pwrseq_adc, GEN_PS_ENTRY,
					PWR_SIG_SRC_ADC, PWR_SIG_TAG_ADC)
};

#define PWR_SIGNAL_POLLED(id) PWR_SIGNAL_ENUM(id),

/*
 * List of power signals that need to be polled.
 */
static const uint8_t polled_signals[] = { DT_FOREACH_STATUS_OKAY(
	intel_ap_pwrseq_external, PWR_SIGNAL_POLLED) };

/*
 * Bitmasks of power signals. A previous copy is held so that
 * logging of changes can occur if the signal is in the debug mask.
 */
static atomic_t power_signals, prev_power_signals;

static power_signal_mask_t debug_signals;

void power_set_debug(power_signal_mask_t debug)
{
	debug_signals = debug;
	/* Copy the current values */
	atomic_set(&prev_power_signals, atomic_get(&power_signals));
}

power_signal_mask_t power_get_debug(void)
{
	return debug_signals;
}

static inline void check_debug(enum power_signal signal)
{
	/*
	 * Only check for debug display if the logging level requires it.
	 */
	if ((CONFIG_AP_PWRSEQ_LOG_LEVEL >= LOG_LEVEL_INF) &&
	    (debug_signals & POWER_SIGNAL_MASK(signal))) {
		bool value = atomic_test_bit(&power_signals, signal);

		if (value != atomic_test_bit(&prev_power_signals, signal)) {
			LOG_INF("%s -> %d", power_signal_name(signal), value);
			atomic_set_bit_to(&prev_power_signals, signal, value);
		}
	}
}

power_signal_mask_t power_get_signals(void)
{
	power_signal_mask_t mask = 0;

	for (int i = 0; i < ARRAY_SIZE(polled_signals); i++) {
		if (power_signal_get(polled_signals[i])) {
			mask |= POWER_SIGNAL_MASK(polled_signals[i]);
		}
	}
	return mask | atomic_get(&power_signals);
}

void power_signal_interrupt(enum power_signal signal, int value)
{
	atomic_set_bit_to(&power_signals, signal, value);
	check_debug(signal);
	ap_pwrseq_wake();
}

int power_wait_mask_signals_timeout(power_signal_mask_t mask,
				    power_signal_mask_t want, int timeout)
{
	if (mask == 0) {
		return 0;
	}
	want &= mask;
	while (timeout-- > 0) {
		if ((power_get_signals() & mask) == want) {
			return 0;
		}
		k_msleep(1);
	}
	return -ETIMEDOUT;
}

int power_signal_get(enum power_signal signal)
{
	const struct ps_config *cp;

	if (signal < 0 || signal >= POWER_SIGNAL_COUNT) {
		return -EINVAL;
	}
	cp = &sig_config[signal];
	switch (cp->source) {
	default:
		return -EINVAL; /* should never happen */

#if HAS_GPIO_SIGNALS
	case PWR_SIG_SRC_GPIO:
		return power_signal_gpio_get(cp->src_enum);
#endif

#if HAS_VW_SIGNALS
	case PWR_SIG_SRC_VW:
		return power_signal_vw_get(cp->src_enum);
#endif

#if HAS_EXT_SIGNALS
	case PWR_SIG_SRC_EXT:
		return board_power_signal_get(signal);
#endif

#if HAS_ADC_SIGNALS
	case PWR_SIG_SRC_ADC:
		return power_signal_adc_get(cp->src_enum);
#endif
	}
}

int power_signal_set(enum power_signal signal, int value)
{
	const struct ps_config *cp;
	int ret;

	if (signal < 0 || signal >= POWER_SIGNAL_COUNT) {
		return -EINVAL;
	}
	cp = &sig_config[signal];
	LOG_DBG("Set %s to %d", power_signal_name(signal), value);
	switch (cp->source) {
	default:
		return -EINVAL;

#if HAS_GPIO_SIGNALS
	case PWR_SIG_SRC_GPIO:
		ret = power_signal_gpio_set(cp->src_enum, value);
		break;
#endif

#if HAS_EXT_SIGNALS
	case PWR_SIG_SRC_EXT:
		ret = board_power_signal_set(signal, value);
		break;
#endif
	}
	/*
	 * Output succeeded, update mask.
	 */
	if (ret == 0) {
		atomic_set_bit_to(&power_signals, signal, value);
		check_debug(signal);
	}
	return ret;
}

int power_signal_enable(enum power_signal signal)
{
	const struct ps_config *cp;

	if (signal < 0 || signal >= POWER_SIGNAL_COUNT) {
		return -EINVAL;
	}
	cp = &sig_config[signal];
	switch (cp->source) {
	default:
		/*
		 * Not sure if board (external) signals will
		 * need interrupt enable/disable.
		 */
		return -EINVAL;

#if HAS_GPIO_SIGNALS
	case PWR_SIG_SRC_GPIO:
		return power_signal_gpio_enable(cp->src_enum);
#endif
#if HAS_ADC_SIGNALS
	case PWR_SIG_SRC_ADC:
		return power_signal_adc_enable(cp->src_enum);
#endif
	}
}

int power_signal_disable(enum power_signal signal)
{
	const struct ps_config *cp;

	if (signal < 0 || signal >= POWER_SIGNAL_COUNT) {
		return -EINVAL;
	}
	cp = &sig_config[signal];
	switch (cp->source) {
	default:
		return -EINVAL;

#if HAS_GPIO_SIGNALS
	case PWR_SIG_SRC_GPIO:
		return power_signal_gpio_disable(cp->src_enum);
#endif
#if HAS_ADC_SIGNALS
	case PWR_SIG_SRC_ADC:
		return power_signal_adc_disable(cp->src_enum);
#endif
	}
}

const char *power_signal_name(enum power_signal signal)
{
	if (signal < 0 || signal >= POWER_SIGNAL_COUNT) {
		return NULL;
	}
	return sig_config[signal].debug_name;
}

void power_signal_init(void)
{
	if (IS_ENABLED(HAS_GPIO_SIGNALS)) {
		power_signal_gpio_init();
	}
	if (IS_ENABLED(HAS_VW_SIGNALS)) {
		power_signal_vw_init();
	}
	if (IS_ENABLED(HAS_ADC_SIGNALS)) {
		power_signal_adc_init();
	}
	/*
	 * Initialise the mask with the current values.
	 * This includes the outputs as well.
	 */
	for (int i = 0; i < POWER_SIGNAL_COUNT; i++) {
		if (power_signal_get(i) == 1) {
			atomic_set_bit(&power_signals, i);
		}
	}
	/*
	 * Some signals are polled (such as the board external signals),
	 * so clear these values from the initial state so they
	 * don't get OR'ed in later on.
	 */
	for (int i = 0; i < ARRAY_SIZE(polled_signals); i++) {
		atomic_clear_bit(&power_signals, polled_signals[i]);
	}
	/*
	 * Save the current state so that new changes can be
	 * checked against the debug mask.
	 */
	atomic_set(&prev_power_signals, atomic_get(&power_signals));
}
