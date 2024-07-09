/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_TCPCI_I2C_MOCK_H
#define __MOCK_TCPCI_I2C_MOCK_H

#include "common.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_TCPCI_I2C_ADDR_FLAGS 0x99

void mock_tcpci_reset(void);

void mock_tcpci_set_reg(int reg, uint16_t value);
void mock_tcpci_set_reg_bits(int reg_offset, uint16_t mask);
void mock_tcpci_clr_reg_bits(int reg_offset, uint16_t mask);

uint16_t mock_tcpci_get_reg(int reg_offset);

int verify_tcpci_transmit(enum tcpci_msg_type tx_type,
			  enum pd_ctrl_msg_type ctrl_msg,
			  enum pd_data_msg_type data_msg);

int verify_tcpci_tx_retry_count(enum tcpci_msg_type tx_type,
				enum pd_ctrl_msg_type ctrl_msg,
				enum pd_data_msg_type data_msg,
				int retry_count);

int verify_tcpci_tx_timeout(enum tcpci_msg_type tx_type,
			    enum pd_ctrl_msg_type ctrl_msg,
			    enum pd_data_msg_type data_msg, int timeout);

int verify_tcpci_tx_with_data(enum tcpci_msg_type tx_type,
			      enum pd_data_msg_type data_msg, uint8_t *data,
			      int data_bytes, int *msg_len, int timeout);

struct possible_tx {
	enum tcpci_msg_type tx_type;
	enum pd_ctrl_msg_type ctrl_msg;
	enum pd_data_msg_type data_msg;
};

int verify_tcpci_possible_tx(struct possible_tx possible[], int possible_cnt,
			     int *found_index, uint8_t *data, int data_bytes,
			     int *msg_len, int timeout);

void mock_tcpci_receive(enum tcpci_msg_type sop, uint16_t header,
			uint32_t *payload);

void tcpci_register_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_TCPCI_I2C_MOCK_H */