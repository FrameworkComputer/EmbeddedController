/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_PWRSEQ_SIGNAL_VW_H__
#define __AP_PWRSEQ_SIGNAL_VW_H__

#define PWR_SIG_TAG_VW	PWR_VW_

/*
 * Generate enums for the virtual wire signals.
 * These enums are only used internally
 * to assign an index to each signal that is specific
 * to the source.
 */

#define TAG_VW(tag, name) DT_CAT(tag, name)

#define PWR_VW_ENUM(id) TAG_VW(PWR_SIG_TAG_VW, PWR_SIGNAL_ENUM(id)),

enum pwr_sig_vw {
#if HAS_VW_SIGNALS
DT_FOREACH_STATUS_OKAY(intel_ap_pwrseq_vw, PWR_VW_ENUM)
#endif
	PWR_SIG_VW_COUNT
};

#undef	PWR_VW_ENUM
#undef	TAG_VW

/**
 * @brief Get the value of the virtual wire signal.
 *
 * @param index The VW signal index to get.
 * @return the current value of the virtual wire.
 */
int power_signal_vw_get(enum pwr_sig_vw vw);

/**
 * @brief Initialize the power signal interface.
 *
 * Called when the power sequence code is ready to start
 * processing inputs and outputs.
 */
void power_signal_vw_init(void);

/**
 * @brief External notification when the bus is ready or not.
 *
 * @param ready true When signals are valid, false when bus is not ready.
 */
void notify_espi_ready(bool ready);

#endif /* __AP_PWRSEQ_SIGNAL_VW_H__ */
