/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BODY_DETECTION_TEST_DATA_H
#define __CROS_EC_BODY_DETECTION_TEST_DATA_H

#include "body_detection.h"
#include "motion_sense.h"

struct body_detect_test_data {
	float x, y, z;
	int action;
};

extern const struct body_detect_test_data kBodyDetectOnBodyTestData[];
extern const size_t kBodyDetectOnBodyTestDataLength;

extern const struct body_detect_test_data kBodyDetectOffOnTestData[];
extern const size_t kBodyDetectOffOnTestDataLength;

extern const struct body_detect_test_data kBodyDetectOnOffTestData[];
extern const size_t kBodyDetectOnOffTestDataLength;
#endif
