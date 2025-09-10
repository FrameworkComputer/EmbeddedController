/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BODY_DETECTION_CLIENT_H
#define __CROS_EC_BODY_DETECTION_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum body_detect_states { BODY_DETECTION_OFF_BODY, BODY_DETECTION_ON_BODY };

/* get the state of body detection */
enum body_detect_states body_detect_get_state(void);
/* set the state of body detection, call hooks.
 * Run on the EC that implement the body detection stack as well
 * as the remote client, if any.
 */
void body_detect_change_state_extern(enum body_detect_states state);
/* print the state on the console. */
void print_body_detect_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BODY_DETECTION_CLIENT_H */
