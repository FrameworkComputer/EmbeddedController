/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection_client.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"

#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)

static enum body_detect_states motion_state = BODY_DETECTION_ON_BODY;

enum body_detect_states body_detect_get_state(void)
{
	return motion_state;
}

void print_body_detect_mode(void)
{
	CPRINTS("body detect mode %sabled",
		body_detect_get_state() ? "en" : "dis");
}

void body_detect_change_state_extern(enum body_detect_states state)
{
	/* change the motion state */
	motion_state = state;
	print_body_detect_mode();

	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MODE_CHANGE)) {
		host_set_single_event(EC_HOST_EVENT_BODY_DETECT_CHANGE);
	}

	hook_notify(HOOK_BODY_DETECT_CHANGE);
}
