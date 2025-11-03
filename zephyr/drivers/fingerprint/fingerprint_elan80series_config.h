/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_FINGERPRINT_ELAN80SERIES_CONFIG_H
#define __CROS_FINGERPRINT_ELAN80SERIES_CONFIG_H

/*
 * Select the correct configuration file based on the enabled ELAN80 series
 * fingerprint sensor.
 */

#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
#include "fingerprint_elan80sg_config.h"
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELANI80SA)
#include "fingerprint_elani80sa_config.h"
#else
#error "No valid configuration for fingerprint sensor."
#endif

#endif /* __CROS_FINGERPRINT_ELAN80SERIES_CONFIG_H */
