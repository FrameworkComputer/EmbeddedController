/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

enum host_event_code {
	EC_HOST_EVENT_KEYBOARD_RECOVERY,
	EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT,
};
void host_set_single_event(enum host_event_code event);
