/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bluetooth LE packet formats, etc. */

/*
 * Since the fields are all little endian,
 *
 * uint16_t two_octets;
 *
 * is used in place of
 *
 * uint8_t two_single_octets[2];
 *
 * in many places.
 */

#ifndef __CROS_EC_BLE_H
#define __CROS_EC_BLE_H

#include "common.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLUETOOTH_ADDR_OCTETS 6

/*
 * GAP assigned numbers
 * https://www.bluetooth.org/en-us/specification/
 * assigned-numbers/generic-access-profile
 */
#define GAP_FLAGS 0x01
#define GAP_INCOMP_16_BIT_UUID 0x02
#define GAP_COMP_16_BIT_UUID 0x03
#define GAP_INCOMP_32_BIT_UUID 0x04
#define GAP_COMP_32_BIT_UUID 0x05
#define GAP_INCOMP_128_BIT_UUID 0x06
#define GAP_COMP_128_BIT_UUID 0x07
#define GAP_SHORT_NAME 0x08
#define GAP_COMPLETE_NAME 0x09
#define GAP_TX_POWER_LEVEL 0x0A
#define GAP_CLASS_OF_DEVICE 0x0D
#define GAP_SIMPLE_PAIRING_HASH 0x0E
#define GAP_SIMPLE_PAIRING_HASH_192 0x0E
#define GAP_SIMPLE_PAIRING_RAND 0x0F
#define GAP_SIMPLE_PAIRING_RAND_192 0x0F
#define GAP_DEVICE_ID 0x10
#define GAP_SECURITY_MANAGER_TK 0x10
#define GAP_SECURITY_MANAGER_OOB_FLAGS 0x11
#define GAP_SLAVE_CONNECTION_INTERVAL_RANGE 0x12
#define GAP_SERVICE_SOLICITATION_UUID_16 0x14
#define GAP_SERVICE_SOLICITATION_UUID_32 0x1F
#define GAP_SERVICE_SOLICITATION_UUID_128 0x15
#define GAP_SERVICE_DATA 0x16
#define GAP_SERVICE_DATA_UUID_16 0x16
#define GAP_SERVICE_DATA_UUID_32 0x20
#define GAP_SERVICE_DATA_UUID_128 0x21
#define GAP_LE_SECURE_CONNECTIONS_CONFIRMATION 0x22
#define GAP_LE_SECURE_CONNECTIONS_RAND 0x23
#define GAP_PUBLIC_TARGET_ADDRESS 0x17
#define GAP_RANDOM_TARGET_ADDRESS 0x18
#define GAP_APPEARANCE 0x19
#define GAP_ADVERTISING_INTERVAL 0x1A
#define GAP_LE_BLUETOOTH_DEVICE_ADDRESS 0x1B
#define GAP_LE_ROLE 0x1C
#define GAP_SIMPLE_PAIRING_HASH_256 0x1D
#define GAP_SIMPLE_PAIRING_RAND_256 0x1E
#define GAP_3D_INFORMATION_DATA 0x3D
#define GAP_MANUFACTURER_SPECIFIC_DATA 0xFF

/* org.bluetooth.characteristic.gap.appearance.xml */
#define GAP_APPEARANCE_HID_KEYBOARD 961

/* org.bluetooth.service.human_interface_device.xml */
#define GATT_SERVICE_HID_UUID 0x1812

/* Bluetooth Core Supplement v5 */

/* Bluetooth Core Supplement v5 1.3 */
#define GAP_FLAGS_LE_LIM_DISC 0x01
#define GAP_FLAGS_LE_GEN_DISC 0x02
#define GAP_FLAGS_LE_NO_BR_EDR 0x04

/* Bluetooth Core Supplement v5 1.3 */

/* BLE 4.1 Vol 6 section 2.3 pg 38+ */

/* Advertising PDU Header
 * 16 Bits:
 *    4 bit type
 *    1 bit TxAddr
 *    1 bit RxAddr
 *    6 bit length (length of the payload in bytes)
 */

struct ble_adv_header {
	uint8_t type;
	uint8_t txaddr;
	uint8_t rxaddr;
	uint8_t length;
};

#define BLE_ADV_HEADER_PDU_TYPE_SHIFT 0
#define BLE_ADV_HEADER_TXADD_SHIFT 6
#define BLE_ADV_HEADER_RXADD_SHIFT 7
#define BLE_ADV_HEADER_LENGTH_SHIFT 8

#define BLE_ADV_HEADER(type, tx, rx, length)                             \
	((uint16_t)((((length) & 0x3f) << BLE_ADV_HEADER_LENGTH_SHIFT) | \
		    (((rx) & 0x1) << BLE_ADV_HEADER_RXADD_SHIFT) |       \
		    (((tx) & 0x1) << BLE_ADV_HEADER_TXADD_SHIFT) |       \
		    (((type) & 0xf) << BLE_ADV_HEADER_PDU_TYPE_SHIFT)))

#define BLE_ADV_HEADER_PDU_TYPE_ADV_IND 0
#define BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND 1
#define BLE_ADV_HEADER_PDU_TYPE_ADV_NONCONN_IND 2
#define BLE_ADV_HEADER_PDU_TYPE_SCAN_REQ 3
#define BLE_ADV_HEADER_PDU_TYPE_SCAN_RSP 4
#define BLE_ADV_HEADER_PDU_TYPE_CONNECT_REQ 5
#define BLE_ADV_HEADER_PDU_TYPE_ADV_SCAN_IND 6

#define BLE_ADV_HEADER_PUBLIC_ADDR 0
#define BLE_ADV_HEADER_RANDOM_ADDR 1

/* BLE 4.1 Vol 3 Part C 10.8 */
#define BLE_RANDOM_ADDR_MSBS_PRIVATE 0x00
#define BLE_RANDOM_ADDR_MSBS_RESOLVABLE_PRIVATE 0x40
#define BLE_RANDOM_ADDR_MSBS_RFU 0x80
#define BLE_RANDOM_ADDR_MSBS_STATIC 0xC0

#define BLE_ADV_ACCESS_ADDRESS 0x8E89BED6
#define BLE_ADV_CRCINIT 0x555555

#define BLE_MAX_ADV_PAYLOAD_OCTETS 37

/* LL SCA Values.  They are shifted left 5 bits for Hop values */
#define BLE_LL_SCA_251_PPM_TO_500_PPM (0 << 5)
#define BLE_LL_SCA_151_PPM_TO_250_PPM BIT(5)
#define BLE_LL_SCA_101_PPM_TO_150_PPM (2 << 5)
#define BLE_LL_SCA_076_PPM_TO_100_PPM (3 << 5)
#define BLE_LL_SCA_051_PPM_TO_075_PPM (4 << 5)
#define BLE_LL_SCA_031_PPM_TO_050_PPM (5 << 5)
#define BLE_LL_SCA_021_PPM_TO_030_PPM (6 << 5)
#define BLE_LL_SCA_000_PPM_TO_020_PPM (7 << 5)

/* BLE 4.1 Vol 6 section 2.4 pg 45 */

/* Data PDU Header
 * 16 Bits:
 *    2 bit LLID   ( Control or Data )
 *    1 bit NESN   ( Next expected sequence number )
 *    1 bit SN     ( Sequence Number )
 *    1 bit MD     ( More Data )
 *    5 bit length ( length of the payload + MIC in bytes )
 *
 * This struct isn't packed, since it isn't sent to the radio.
 *
 */

struct ble_data_header {
	uint8_t llid;
	uint8_t nesn;
	uint8_t sn;
	uint8_t md;
	uint8_t length;
};

#define BLE_DATA_HEADER_LLID_SHIFT 0
#define BLE_DATA_HEADER_NESN_SHIFT 2
#define BLE_DATA_HEADER_SN_SHIFT 3
#define BLE_DATA_HEADER_MD_SHIFT 4
#define BLE_DATA_HEADER_LENGTH_SHIFT 8

#define BLE_DATA_HEADER_LLID_DATANOSTART 1
#define BLE_DATA_HEADER_LLID_DATASTART 2
#define BLE_DATA_HEADER_LLID_CONTROL 3

#define BLE_DATA_HEADER(llid, nesn, sn, md, length)                       \
	((uint16_t)((((length) & 0x1f) << BLE_DATA_HEADER_LENGTH_SHIFT) | \
		    (((MD) & 0x1) << BLE_DATA_HEADER_MD_SHIFT) |          \
		    (((SN) & 0x1) << BLE_DATA_HEADER_SN_SHIFT) |          \
		    (((NESN) & 0x1) << BLE_DATA_HEADER_NESN_SHIFT) |      \
		    (((llid) & 0x3) << BLE_DATA_HEADER_LLID_SHIFT)))

#define BLE_MAX_DATA_PAYLOAD_OCTETS 31
#define BLE_MAX_PAYLOAD_OCTETS BLE_MAX_ADV_PAYLOAD_OCTETS

union ble_header {
	struct ble_adv_header adv;
	struct ble_data_header data;
};

struct ble_pdu {
	union ble_header header;
	uint8_t header_type_adv;
	uint8_t payload[BLE_MAX_PAYLOAD_OCTETS];
	uint32_t mic; /* Only included in PDUs with encrypted payloads. */
};

struct ble_packet {
	/* uint8_t preamble; */
	uint32_t access_address;
	struct ble_pdu pdu;
	/* uint32_t crc; */
};

/* LL Control PDU Opcodes BLE 4.1 Vol 6 2.4.2 */
#define BLE_LL_CONNECTION_UPDATE_REQ 0x00
#define BLE_LL_CHANNEL_MAP_REQ 0x01
#define BLE_LL_TERMINATE_IND 0x02
#define BLE_LL_ENC_REQ 0x03
#define BLE_LL_ENC_RSP 0x04
#define BLE_LL_START_ENC_REQ 0x05
#define BLE_LL_START_ENC_RSP 0x06
#define BLE_LL_UNKNOWN_RSP 0x07
#define BLE_LL_FEATURE_REQ 0x08
#define BLE_LL_FEATURE_RSP 0x09
#define BLE_LL_PAUSE_ENC_REQ 0x0A
#define BLE_LL_PAUSE_ENC_RSP 0x0B
#define BLE_LL_VERSION_IND 0x0C
#define BLE_LL_REJECT_IND 0x0D
#define BLE_LL_SLAVE_FEATURE_REQ 0x0E
#define BLE_LL_CONNECTION_PARAM_REQ 0x0F
#define BLE_LL_CONNECTION_PARAM_RSP 0x10
#define BLE_LL_REJECT_IND_EXT 0x11
#define BLE_LL_PING_REQ 0x12
#define BLE_LL_PING_RSP 0x13
#define BLE_LL_RFU 0x14

/* BLE 4.1 Vol 6 4.6 Table 4.3 */
#define BLE_LL_FEATURE_LE_ENCRYPTION 0x00
#define BLE_LL_FEATURE_CONN_PARAMS_REQ 0x01
#define BLE_LL_FEATURE_EXT_REJ_IND 0x02
#define BLE_LL_FEATURE_SLAVE_FEAT_EXCHG 0x03
#define BLE_LL_FEATURE_LE_PING 0x04

struct ble_ll_connection_update_req {
	uint8_t win_size;
	uint16_t win_offset;
	uint16_t interval;
	uint16_t latency;
	uint16_t timeout;
	uint16_t instant;
} __packed;

struct ble_ll_channel_map_req {
	uint8_t map[5];
	uint16_t instant;
} __packed;

/* ble_ll_terminate_ind: single-byte error code */

struct ble_ll_enc_req {
	uint8_t rand[8];
	uint16_t ediv;
	uint8_t skdm[8];
	uint8_t ivm[4];
} __packed;

struct ble_ll_enc_rsp {
	uint8_t skds[8];
	uint8_t ivs[4];
} __packed;

/* ble_ll_start_enc_req has no CtrData field */

/* ble_ll_start_enc_rsp has no CtrData field */

/* ble_ll_unknown_rsp: single-byte error code */

struct ble_ll_feature_req {
	uint8_t feature_set[8];
} __packed;

struct ble_ll_feature_rsp {
	uint8_t feature_set[8];
} __packed;

/* ble_ll_pause_enc_req has no CtrData field */

/* ble_ll_pause_enc_rsp has no CtrData field */

#define BLE_LL_VERS_NR_4_0 6
#define BLE_LL_VERS_NR_4_1 7

struct ble_ll_version_ind {
	uint8_t vers_nr; /* Version Number */
	uint16_t comp_id; /* Company ID */
	uint16_t sub_vers_nr; /* Subversion Number */
} __packed;

/* ble_ll_reject_ind: single-byte error code */

struct ble_ll_slave_feature_req {
	uint8_t feature_set[8];
} __packed;

/* ble_ll_connection_param (req and rsp) */

struct ble_ll_connection_param {
	uint16_t interval_min; /* times 1.25 ms */
	uint16_t interval_max; /* times 1.25 ms */
	uint16_t latency; /* connection events */
	uint16_t timeout; /* times 10 ms */
	uint8_t preferred_periodicity; /* times 1.25 ms */
	uint16_t reference_conn_event_count; /* base for offsets*/
	uint16_t offset0; /* Anchor offset from reference (preferred) */
	uint16_t offset1;
	uint16_t offset2;
	uint16_t offset3;
	uint16_t offset4;
	uint16_t offset5; /* least preferred */
} __packed;

struct ble_ll_reject_ind_ext {
	uint8_t reject_opcode;
	uint8_t error_code;
} __packed;

/* ble_ll_ping_req has no CtrData field */

/* ble_ll_ping_rsp has no CtrData field */

/* BLE 4.1 Vol 6 4.5.8 */
struct remapping_table {
	uint8_t remapping_index[37];
	uint8_t map[5];
	int num_used_channels;
	int hop_increment;
	int last_unmapped_channel;
};

/* BLE 4.1 Vol 6 4.5.9 */
struct connection_data {
	int transmit_seq_num;
	int next_expected_seq_num;
	struct remapping_table rt;
	/* Add timing information */
};

/* BLE 4.1 Vol 6 1.4.1 */
int chan2freq(int channel);

/* BLE 4.1 Vol 6 2.3.3.1 */
void fill_remapping_table(struct remapping_table *rt, uint8_t map[5],
			  int hop_increment);

void ble_tx(struct ble_pdu *pdu);

/**
 * Receive a packet into pdu if one comes before the timeout
 *
 * @param	pdu Where the received data is to be stored
 * @param	timeout Number of microseconds allowed before timeout
 * @param	adv Set to 1 if receiving in advertising state; else set to 0
 * @returns EC_SUCCESS on packet reception, else returns error
 */
int ble_rx(struct ble_pdu *pdu, int timeout, int adv);

int ble_radio_init(uint32_t access_address, uint32_t crc_init_val);

/*
 * Uses the algorithm defined in the BLE core specifcation
 * 4.1 Vol 6 4.5.8 to select the next data channel
 */
uint8_t get_next_data_channel(struct remapping_table *rt);

/* BLE 4.1 Vol 3 Part C 11 */
uint8_t *pack_adv(uint8_t *dest, int length, int type, const uint8_t *data);
uint8_t *pack_adv_int(uint8_t *dest, int length, int type, int data);
uint8_t *pack_adv_addr(uint8_t *dest, uint64_t addr);

const uint8_t *unpack_adv(const uint8_t *src, int *length, int *type,
			  const uint8_t **data);

void dump_ble_addr(uint8_t *mem, char *name);

void dump_ble_packet(struct ble_pdu *ble_p);

/* Radio-specific allow list handling */
int ble_radio_clear_allow_list(void);
int ble_radio_read_allow_list_size(uint8_t *ret_size);
int ble_radio_add_device_to_allow_list(const uint8_t *addr_ptr, uint8_t rand);
int ble_radio_remove_device_from_allow_list(const uint8_t *addr_ptr,
					    uint8_t rand);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BLE_H */
