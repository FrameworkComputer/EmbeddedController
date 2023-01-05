/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* host command customization configuration */

#ifndef __BASEBOARD_HOST_COMMANDS_H
#define __BASEBOARD_HOST_COMMANDS_H

#define EC_CMD_PRIVACY_SWITCHES_CHECK_MODE 0x3E14

struct ec_response_privacy_switches_check {
	uint8_t microphone;
	uint8_t camera;
} __ec_align1;

#endif /* __BASEBOARD_HOST_COMMANDS_H */
