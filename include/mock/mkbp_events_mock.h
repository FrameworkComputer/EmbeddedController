/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock MKBP keyboard protocol
 */

#ifndef __MOCK_MKBP_EVENTS_MOCK_H
#define __MOCK_MKBP_EVENTS_MOCK_H

struct mock_ctrl_mkbp_events {
	int mkbp_send_event_return;
};

#define MOCK_CTRL_DEFAULT_MKBP_EVENTS          \
(struct mock_ctrl_mkbp_events) {               \
	.mkbp_send_event_return = 1,           \
}

extern struct mock_ctrl_mkbp_events mock_ctrl_mkbp_events;

#endif /* __MOCK_MKBP_EVENTS_MOCK_H */
