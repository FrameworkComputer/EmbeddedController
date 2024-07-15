/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_RAURU_INCLUDE_RAURU_DP_H_
#define ZEPHYR_RAURU_INCLUDE_RAURU_DP_H_

enum rauru_dp_port {
	DP_PORT_NONE = -1,
	DP_PORT_C0 = 0,
	DP_PORT_C1,
	DP_PORT_HDMI,
	DP_PORT_COUNT,
};

int rauru_is_dp_muxable(enum rauru_dp_port port);
void rauru_set_dp_path(int port);
enum rauru_dp_port rauru_get_dp_path(void);
bool rauru_is_hpd_high(enum rauru_dp_port port);
bool rauru_has_hdmi_port(void);
/* Detach and rotate the DP path */
void rauru_detach_dp_path(enum rauru_dp_port port);
#endif /* ZEPHYR_RAURU_INCLUDE_RAURU_DP_H_ */
