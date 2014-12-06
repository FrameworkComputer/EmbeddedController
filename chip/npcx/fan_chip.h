/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific MFT module for Chrome EC */

#ifndef __CROS_EC_NPCX_FAN_H
#define __CROS_EC_NPCX_FAN_H

/* MFT module select */
enum npcx_mft_module {
	NPCX_MFT_MODULE_1 = 0,
	NPCX_MFT_MODULE_2 = 0,
	NPCX_MFT_MODULE_3 = 0,
	/* Number of MFT modules */
	NPCX_MFT_MODULE_COUNT
};

/* MFT module port */
enum npcx_mft_module_port {
	NPCX_MFT_MODULE_PORT_TA,
	NPCX_MFT_MODULE_PORT_TB,
	/* Number of MFT module ports */
	NPCX_MFT_MODULE_PORT_COUNT
};

/* Data structure to define MFT channels. */
struct mft_t {
	/* MFT module ID */
	enum npcx_mft_module module;
	/* MFT port */
	enum npcx_mft_module_port port;
	/* MFT TCNT default count */
	uint32_t default_count;
	/* MFT freq */
	uint32_t freq;
};

/* Tacho measurement state */
enum tacho_measure_state {
	/* Tacho init state */
	TACHO_IN_IDLE = 0,
	/* Tacho first edge state */
	TACHO_WAIT_FOR_1_EDGE,
	/* Tacho second edge state */
	TACHO_WAIT_FOR_2_EDGE,
	/* Tacho underflow state */
	TACHO_UNDERFLOW
};

/* Tacho status data structure */
struct tacho_status_t {
	/* Current state of the measurement */
	enum tacho_measure_state cur_state;
	/* Pulse counter value between edge1 and edge2 */
	uint32_t edge_interval;
};

extern const struct mft_t mft_channels[];

#endif /* __CROS_EC_NPCX_FAN_H */
