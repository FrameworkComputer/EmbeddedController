/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "radio.h"

int radio_disable(void)
{
	int timeout = 10000;

	NRF51_RADIO_DISABLED = 0;
	NRF51_RADIO_DISABLE = 1;

	while (!NRF51_RADIO_DISABLED && timeout > 0)
		timeout--;

	if (timeout == 0)
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}

int radio_init(enum nrf51_radio_mode_t mode)
{
	int err_code = radio_disable();

	if (mode == BLE_1MBIT) {
		NRF51_RADIO_MODE = NRF51_RADIO_MODE_BLE_1MBIT;

		NRF51_RADIO_TIFS = 150;	/* Bluetooth 4.1 Vol 6 pg 58 4.1 */

		/*
		 * BLE never sends or receives two packets in a row.
		 * Enabling the radio means we want to transmit or receive.
		 * After transmission, disable as quickly as possible.
		 */
		NRF51_RADIO_SHORTS = NRF51_RADIO_SHORTS_READY_START |
				NRF51_RADIO_SHORTS_END_DISABLE;

		/* Use factory parameters if available */
		if (!(NRF51_FICR_OVERRIDEEN & NRF51_FICR_OVERRIDEEN_BLE_BIT_N)
		    ) {
			int i;

			for (i = 0; i < 4; i++) {
				NRF51_RADIO_OVERRIDE(i) =
					NRF51_FICR_BLE_1MBIT(i);
			}
			NRF51_RADIO_OVERRIDE(4) = NRF51_FICR_BLE_1MBIT(4) |
				NRF51_RADIO_OVERRIDE_EN;
		}
	} else {
		return EC_ERROR_UNIMPLEMENTED;
	}

	return err_code;
}

