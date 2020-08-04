/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __NRF51_BLUETOOTH_LE_H
#define __NRF51_BLUETOOTH_LE_H

#include "common.h"
#include "include/bluetooth_le.h"

#define NRF51_BLE_LENGTH_BITS 8
#define NRF51_BLE_S0_BYTES    1
#define NRF51_BLE_S1_BITS     0 /* no s1 field */

#define BLE_ACCESS_ADDRESS_BYTES 4
#define EXTRA_RECEIVE_BYTES      0
#define BLE_ADV_WHITEN           1

#define RADIO_SETUP_TIMEOUT 1000

/* Data and Advertisements have the same PCNF values */
#define NRF51_RADIO_PCNF0_ADV_DATA \
			NRF51_RADIO_PCNF0_VAL(NRF51_BLE_LENGTH_BITS, \
			NRF51_BLE_S0_BYTES, \
			NRF51_BLE_S1_BITS)

#define NRF51_RADIO_PCNF1_ADV_DATA \
			NRF51_RADIO_PCNF1_VAL(BLE_MAX_ADV_PAYLOAD_OCTETS, \
		    EXTRA_RECEIVE_BYTES, \
		    BLE_ACCESS_ADDRESS_BYTES - 1, \
		    BLE_ADV_WHITEN)

struct nrf51_ble_packet_t {
	uint8_t s0; /* First byte */
	uint8_t length; /* Length field */
	uint8_t payload[BLE_MAX_DATA_PAYLOAD_OCTETS];
} __packed;

struct nrf51_ble_config_t {
	uint8_t channel;
	uint8_t address;
	uint32_t crc_init;
};

/* Initialize the nRF51 radio for BLE */
int ble_radio_init(uint32_t access_address, uint32_t crc_init_val);

/* Transmit pdu on the radio */
void ble_tx(struct ble_pdu *pdu);

/* Receive a packet into pdu if one comes before the timeout */
int ble_rx(struct ble_pdu *pdu, int timeout, int adv);

/* Allow list handling */

/* Clear the allow list */
int ble_radio_clear_allow_list(void);

/* Read the size of the allow list and assign it to ret_size */
int ble_radio_read_allow_list_size(uint8_t *ret_size);

/* Add the device with the address specified by addr_ptr and type */
int ble_radio_add_device_to_allow_list(const uint8_t *addr_ptr, uint8_t type);

/* Remove the device with the address specified by addr_ptr and type */
int ble_radio_remove_device_from_allow_list(const uint8_t *addr_ptr,
					    uint8_t type);

#endif  /* __NRF51_BLUETOOTH_LE_H */
