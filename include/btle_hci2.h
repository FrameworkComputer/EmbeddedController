/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Copied from NewBlue hci.c with permission from Dmitry Grinberg, the original
 * author.
 */

#ifndef __CROS_EC_BTLE_HCI2_H
#define __CROS_EC_BTLE_HCI2_H

#include "btle_hci_int.h"

struct hciCmdHdr {
	uint16_t opcode;
	uint8_t paramLen;
} __packed;
#define CMD_MAKE_OPCODE(ogf, ocf) \
	((((uint16_t)((ogf) & 0x3f)) << 10) | ((ocf) & 0x03ff))
#define CMD_GET_OGF(opcode) (((opcode) >> 10) & 0x3f)
#define CMD_GET_OCF(opcode) ((opcode) & 0x03ff)

struct hciAclHdr {
	uint16_t hdr;
	uint16_t len;
} __packed;
#define ACL_HDR_MASK_CONN_ID 0x0FFF
#define ACL_HDR_MASK_PB 0x3000
#define ACL_HDR_MASK_BC 0xC000
#define ACL_HDR_PB_FIRST_NONAUTO 0x0000
#define ACL_HDR_PB_CONTINUED 0x1000
#define ACL_HDR_PB_FIRST_AUTO 0x2000
#define ACL_HDR_PB_COMPLETE 0x3000

struct hciScoHdr {
	uint16_t hdr;
	uint8_t len;
} __packed;
#define SCO_HDR_MASK_CONN_ID 0x0FFF
#define SCO_HDR_MASK_STATUS 0x3000
#define SCO_STATUS_ALL_OK 0x0000
#define SCO_STATUS_UNKNOWN 0x1000
#define SCO_STATUS_NO_DATA 0x2000
#define SCO_STATUS_SOME_DATA 0x3000

struct hciEvtHdr {
	uint8_t code;
	uint8_t len;
} __packed;

void hci_cmd(uint8_t *hciCmdbuf);
void hci_acl_to_host(uint8_t *data, uint16_t hdr, uint16_t len);
void hci_acl_from_host(uint8_t *hciAclbuf);
void hci_event(uint8_t event_code, uint8_t len, uint8_t *params);

#endif /* __CROS_EC_BTLE_HCI2_H */