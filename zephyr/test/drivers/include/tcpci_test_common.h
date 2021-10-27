/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TCPCI_TEST_COMMON_H
#define __TCPCI_TEST_COMMON_H

#include "stubs.h"

/**
 * @brief Check TCPC register value using zassert API
 *
 * @param emul Pointer to TCPCI emulator
 * @param reg TCPC register address to check
 * @param exp_val Expected value of register
 * @param line Line number to print in case of failure
 */
void check_tcpci_reg_f(const struct emul *emul, int reg, uint16_t exp_val,
		       int line);
#define check_tcpci_reg(emul, reg, exp_val)			\
	check_tcpci_reg_f((emul), (reg), (exp_val), __LINE__)

/**
 * @brief Test TCPCI init and vbus level callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_init(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI release callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_release(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI get cc callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_get_cc(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI set cc callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_cc(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI set polarity callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_polarity(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI set vconn callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_vconn(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI set msg header callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_msg_header(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI rx and sop prime enable callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_rx_detect(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI get raw message from TCPC callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_get_rx_message_raw(const struct emul *emul,
				   enum usbc_port port);

/**
 * @brief Test TCPCI transmitting message from TCPC callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_transmit(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI alert callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_alert(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI alert RX message callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_alert_rx_message(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI auto discharge on disconnect callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_auto_discharge(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI drp toggle callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_drp_toggle(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI get chip info callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_get_chip_info(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI enter low power mode callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_low_power_mode(const struct emul *emul, enum usbc_port port);

/**
 * @brief Test TCPCI set bist test mode callback
 *
 * @param emul Pointer to TCPCI emulator
 * @param port Select USBC port that will be used to obtain tcpm_drv from
 *             tcpc_config
 */
void test_tcpci_set_bist_mode(const struct emul *emul, enum usbc_port port);

#endif /* __TCPCI_TEST_COMMON_H */
