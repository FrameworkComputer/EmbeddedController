/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "usb_pd.h"

#define MOCK_TCPCI_I2C_ADDR_FLAGS 0x99

void mock_tcpci_reset(void);

void mock_tcpci_set_reg(int reg, uint16_t value);

uint16_t mock_tcpci_get_reg(int reg_offset);

int verify_tcpci_transmit(enum tcpm_transmit_type tx_type,
			  enum pd_ctrl_msg_type ctrl_msg,
			  enum pd_data_msg_type data_msg);

int verify_tcpci_tx_retry_count(enum tcpm_transmit_type tx_type,
				int retry_count);

int verify_tcpci_tx_timeout(enum tcpm_transmit_type tx_type,
			    enum pd_ctrl_msg_type ctrl_msg,
			    enum pd_data_msg_type data_msg,
			    int timeout);

void mock_tcpci_receive(enum pd_msg_type sop, uint16_t header,
			uint32_t *payload);
