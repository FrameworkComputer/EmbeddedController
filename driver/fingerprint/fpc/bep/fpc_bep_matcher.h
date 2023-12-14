/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_MATCHER_H_
#define __CROS_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_MATCHER_H_

#include <stdint.h>

/*
 * Constant value for the enrollment data size
 *
 * Size of private fp_bio_enrollment_t
 */

#define FP_ALGORITHM_ENROLLMENT_SIZE_FPC (4)

/*
 * Constant value corresponding to the maximum template size
 * for the sensor. Client template memory allocation must
 * have this size. This includes extra memory for template update.
 *
 * Template size + alignment padding + size of template size variable
 */
#if defined(CONFIG_FP_SENSOR_FPC1025)
#define FP_ALGORITHM_TEMPLATE_SIZE_FPC (5088 + 0 + 4)
#elif defined(CONFIG_FP_SENSOR_FPC1035)
#define FP_ALGORITHM_TEMPLATE_SIZE_FPC (14373 + 3 + 4)
#endif

/* Max number of templates stored / matched against */
#define FP_MAX_FINGER_COUNT_FPC (5)

#endif /* __CROS_EC_DRIVER_FINGERPRINT_FPC_BEP_FPC_BEP_MATCHER_H_ */
