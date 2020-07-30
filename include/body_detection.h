/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BODY_DETECTION_H
#define __CROS_EC_BODY_DETECTION_H

#include <stdint.h>

enum body_detect_states {
	BODY_DETECTION_OFF_BODY,
	BODY_DETECTION_ON_BODY
};

/* get/set the state of body detection */
enum body_detect_states body_detect_get_state(void);
void body_detect_change_state(enum body_detect_states state);

/* Reset the data. This should be called when ODR is changed*/
void body_detect_reset(void);

/* Body detect main function. This should be called when new sensor data come */
void body_detect(void);

/* enable/disable body detection */
void body_detect_set_enable(int enable);

/* get enable state of body detection */
int body_detect_get_enable(void);


#endif /* __CROS_EC_BODY_DETECTION_H */
