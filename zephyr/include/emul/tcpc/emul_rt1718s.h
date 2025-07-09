/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_RT1718S_H
#define __EMUL_RT1718S_H

#include "emul/tcpc/emul_tcpci.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/sys/slist.h>

#define RT1718S_EMUL_REG_COUNT_PER_PAGE 0x100
#define RT1718S_EMUL_MAX_REG_INDEX (RT1718S_EMUL_REG_COUNT_PER_PAGE - 1)

struct set_reg_entry_t {
	struct _snode node;
	int reg;
	uint16_t val;
	int64_t access_time;
};

/** Run-time data used by the emulator */
struct rt1718s_emul_data {
	/* Composite with the tcpc_emul_data to extend it. */
	struct tcpc_emul_data embedded_tcpc_emul_data;
	uint8_t reg_page1[RT1718S_EMUL_REG_COUNT_PER_PAGE];
	uint8_t reg_page2[RT1718S_EMUL_REG_COUNT_PER_PAGE];
	uint8_t current_page;
	uint8_t current_page2_register;
	struct _slist set_private_reg_history;
};

/**
 * @brief Getting each byte of register from rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in rt1718s private
 *                 register or val is NULL
 */
int rt1718s_emul_get_reg(const struct emul *emul, int reg, uint16_t *val);

/**
 * @brief Setting each byte of register from rt1718s emulator
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param reg First byte of last write message
 * @param val byte to write to the reg in emulator
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in rt1718s private
 *                 register or val is NULL
 */
int rt1718s_emul_set_reg(const struct emul *emul, int reg, uint16_t val);

/**
 * @brief Reset the register set history
 *
 * @param emul Pointer to I2C rt1718s emulator
 *
 */
void rt1718s_emul_reset_set_history(const struct emul *emul);

/**
 * @brief Set the device id of
 *
 * @param emul Pointer to I2C rt1718s emulator
 * @param device_id the 16 bits device id
 *
 */
void rt1718s_emul_set_device_id(const struct emul *emul, uint16_t device_id);

#endif /* __EMUL_RT1718S_H */
