/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AMD STT (Skin Temperature Tracking) Manager */

#ifndef __CROS_EC_AMD_STT_H
#define __CROS_EC_AMD_STT_H

#define AMD_STT_WRITE_SENSOR_VALUE_CMD 0x3A

enum amd_stt_pcb_sensor {
	AMD_STT_PCB_SENSOR_APU = 0x0,
	AMD_STT_PCB_SENSOR_REMOTE = 0x1,
	AMD_STT_PCB_SENSOR_GPU = 0x2
};

/**
 * Boards must implement these callbacks for SOC and Ambient temperature.
 * Temperature must be returned in Milli Kelvin.
 * TODO(b/192391025): Replace with direct calls to temp_sensor_read_mk
 */
int board_get_soc_temp_mk(int *temp_mk);
int board_get_ambient_temp_mk(int *temp_mk);

#endif /* __CROS_EC_AMD_STT_H */
