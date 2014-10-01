/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test utilities for USB PD unit test.
 */

#ifndef __USB_PD_TEST_UTIL_H
#define __USB_PD_TEST_UTIL_H

/* Simulate Rx message */
void pd_test_rx_set_preamble(int port, int has_preamble);
void pd_test_rx_msg_append_bits(int port, uint32_t bits, int nb);
void pd_test_rx_msg_append_kcode(int port, uint8_t kcode);
void pd_test_rx_msg_append_sop(int port);
void pd_test_rx_msg_append_eop(int port);
void pd_test_rx_msg_append_4b(int port, uint8_t val);
void pd_test_rx_msg_append_short(int port, uint16_t val);
void pd_test_rx_msg_append_word(int port, uint32_t val);
void pd_simulate_rx(int port);

/* Verify Tx message */
int pd_test_tx_msg_verify_kcode(int port, uint8_t kcode);
int pd_test_tx_msg_verify_sop(int port);
int pd_test_tx_msg_verify_eop(int port);
int pd_test_tx_msg_verify_4b5b(int port, uint8_t b4);
int pd_test_tx_msg_verify_short(int port, uint16_t val);
int pd_test_tx_msg_verify_word(int port, uint32_t val);
int pd_test_tx_msg_verify_crc(int port);

#endif  /* __USB_PD_TEST_UTIL_H */
