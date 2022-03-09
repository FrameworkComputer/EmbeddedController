/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Internal API for sending AP power related event callbacks.
 */

#ifndef __AP_POWER_AP_EVENTS_H__
#define __AP_POWER_AP_EVENTS_H__

/**
 * @brief Dispatch callbacks for an event.
 *
 * @param event The event to invoke callbacks for.
 */
void ap_power_ev_send_callbacks(enum ap_power_events event);

#endif /* __AP_POWER_AP_EVENTS_H__ */
