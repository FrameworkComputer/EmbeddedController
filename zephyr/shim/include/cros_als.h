/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_CROS_EC_ALS_H
#define __ZEPHYR_CROS_EC_ALS_H

#define CM32183_COMPAT nxp_cm32183

#define CM32183_SENSOR_ID(node_id) DT_CAT(CM32183_, node_id)
#define CM32183_SENSOR_ID_WITH_COMMA(node_id) CM32183_SENSOR_ID(node_id),

enum als_id {
	DT_FOREACH_STATUS_OKAY(CM32183_COMPAT, CM32183_SENSOR_ID_WITH_COMMA)
	ALS_COUNT,
};

#endif /* __ZEPHYR_CROS_EC_ALS_H */
