/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Radio test interface for NRF51
 *
 * These functions implement parts of the Direct Test Mode functionality in
 * the Bluetooth Spec.
 */

#ifndef __NRF51_RADIO_TEST_H
#define __NRF51_RADIO_TEST_H

#define BLE_MAX_TEST_PAYLOAD_OCTETS       37
#define BLE_MAX_TEST_CHANNEL              39
#define BLE_MIN_TEST_CHANNEL              0

#define NRF51_RADIO_PCNF0_TEST NRF51_RADIO_PCNF0_ADV_DATA

#define BLE_TEST_WHITEN           0

#define NRF51_RADIO_PCNF1_TEST \
	NRF51_RADIO_PCNF1_VAL(BLE_MAX_TEST_PAYLOAD_OCTETS, \
			      EXTRA_RECEIVE_BYTES, \
			      BLE_ACCESS_ADDRESS_BYTES - 1, \
			      BLE_TEST_WHITEN)

/*
 * Prepare the radio for transmitting packets.  The value of chan must be
 * between 0 and 39 inclusive.  The maximum length is 37.
 */

int ble_test_tx_init(int chan, int type, int len);
int ble_test_rx_init(int chan);
void ble_test_tx(void);
int ble_test_rx(void);
void ble_test_stop(void);

#endif /* __NRF51_RADIO_TEST_H */
