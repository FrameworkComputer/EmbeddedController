/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __TCPCI_TEST_COMMON_H
#define __TCPCI_TEST_COMMON_H

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

#endif /* __TCPCI_TEST_COMMON_H */
