/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Radio interface for Chrome EC */

#ifndef __NRF51_RADIO_H
#define __NRF51_RADIO_H

#include "common.h"
#include "compile_time_macros.h"
#include "registers.h"

#ifndef NRF51_RADIO_MAX_PAYLOAD
	#define NRF51_RADIO_MAX_PAYLOAD 253
#endif

#define RADIO_DONE (NRF51_RADIO_END == 1)

enum nrf51_radio_mode_t {
	BLE_1MBIT = NRF51_RADIO_MODE_BLE_1MBIT,
};

struct nrf51_radio_packet_t {
	uint8_t s0; /* First byte */
	uint8_t length; /* Length field */
	uint8_t s1; /* Bits after length */
	uint8_t payload[NRF51_RADIO_MAX_PAYLOAD];
} __packed;

int radio_init(enum nrf51_radio_mode_t mode);

int radio_disable(void);

#endif  /* __NRF51_RADIO_H */
