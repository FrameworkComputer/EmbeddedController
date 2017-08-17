/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_RDD_H
#define __CROS_RDD_H

/**
 * Initialize RDD module
 */
void init_rdd_state(void);

/**
 * Enable/disable forcing debug accessory detection.
 *
 * When enabled, the RDD module will assert CCD_MODE_L even if the CC value
 * does not indicate a debug accessory is present.
 *
 * @param enable	Enable (1) or disable (0) keepalive.
 */
void force_rdd_detect(int enable);

/**
 * Check if debug accessory detection is forced.
 *
 * @return 1 if keepalive is enabled, 0 if disabled.
 */
int rdd_detect_is_forced(void);

#endif  /* __CROS_RDD_H */
