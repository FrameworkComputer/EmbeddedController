/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/cec/bitbang.h"

/**
 * @brief Start the timer for cec bitbang.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 * @param edge to trigger capture timer interrupt on
 * @param timeout timeout for capture interrupt edge
 */
void cros_cec_bitbang_tmr_cap_start(int port, enum cec_cap_edge edge,
				    int timeout);

/**
 * @brief Stop the timer for cec bitbang.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_tmr_cap_stop(int port);

/**
 * @brief get the time from the timer for cec bitbang.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 *
 * @return time read from the timer
 */
int cros_cec_bitbang_tmr_cap_get(int port);

/**
 * @brief enter debounce state.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_debounce_enable(int port);

/**
 * @brief leave debounce state.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_debounce_disable(int port);

/**
 * @brief Elevate to interrupt context.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_trigger_send(int port);

/**
 * @brief enable the timer for cec bitbang.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_enable_timer(int port);

/**
 * @brief disable the timer for cec bitbang.
 *
 * @param dev Pointer to the device structure for the cec bitbang driver
 * instance.
 * @param port CEC port to control
 */
void cros_cec_bitbang_disable_timer(int port);
