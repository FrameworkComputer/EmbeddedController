/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file drivers/dsp_comms/service/dsp_service.cc
 * @brief Public APIs for dsp_service
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_DSP_SERVICE_H_
#define ZEPHYR_INCLUDE_DRIVERS_DSP_SERVICE_H_

/**
 * Called from the deferred GMR sensor ISR handler when the GMR sensor GPIO
 * level triggers an interrupt so the ISH can be notified.
 *
 * @return none.
 */
void dsp_service_hook_tablet_mode_change(void);

#endif /* ZEPHYR_INCLUDE_DRIVERS_DSP_SERVICE_H_ */
