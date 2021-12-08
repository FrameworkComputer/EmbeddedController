/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C charger emulator
 */

#ifndef __EMUL_CHARGER_H
#define __EMUL_CHARGER_H

#include <emul.h>
#include "emul/emul_tcpci_partner_common.h"
#include "emul/emul_tcpci.h"

/**
 * @brief USB-C charger emulator backend API
 * @defgroup charger_emul USB-C charger emulator
 * @{
 *
 * USB-C charger emulator can be attached to TCPCI emulator. It is able to
 * respond to some TCPM messages. It always attach as source and present
 * source capabilities constructed from given PDOs.
 */

/* Maximum number of PDOs is constrained by PD specification */
#define EMUL_CHARGER_MAX_PDOS	7

/** Structure describing charger emulator */
struct charger_emul_data {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Power data objects returned in source capabilities message */
	uint32_t pdo[EMUL_CHARGER_MAX_PDOS];
};

/** Return values of @ref charger_emul_check_pdos function */
enum check_pdos_res {
	CHARGER_EMUL_CHECK_PDO_OK = 0,
	CHARGER_EMUL_FIRST_PDO_NO_FIXED_5V,
	CHARGER_EMUL_FIXED_VOLT_REPEATED,
	CHARGER_EMUL_FIXED_VOLT_NOT_IN_ORDER,
	CHARGER_EMUL_NON_FIRST_PDO_FIXED_FLAGS,
	CHARGER_EMUL_BATT_VOLT_REPEATED,
	CHARGER_EMUL_BATT_VOLT_NOT_IN_ORDER,
	CHARGER_EMUL_VAR_VOLT_REPEATED,
	CHARGER_EMUL_VAR_VOLT_NOT_IN_ORDER,
	CHARGER_EMUL_PDO_AFTER_ZERO,
};

/**
 * @brief Initialise USB-C charger emulator. Need to be called before any other
 *        function.
 *
 * @param data Pointer to USB-C charger emulator
 */
void charger_emul_init(struct charger_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI
 *
 * @param data Pointer to USB-C charger emulator
 * @param tcpci_emul Poinetr to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error or send source capabilities error
 */
int charger_emul_connect_to_tcpci(struct charger_emul_data *data,
				  const struct emul *tcpci_emul);

/**
 * @brief Check if PDOs of given charger emulator are in correct order
 *
 * @param data Pointer to USB-C charger emulator
 *
 * @return CHARGER_EMUL_CHECK_PDO_OK if PDOs are correct
 * @return CHARGER_EMUL_FIRST_PDO_NO_FIXED_5V if first PDO is not fixed type 5V
 * @return CHARGER_EMUL_FIXED_VOLT_REPEATED if two or more fixed type PDOs have
 *         the same voltage
 * @return CHARGER_EMUL_FIXED_VOLT_NOT_IN_ORDER if fixed PDO with higher voltage
 *         is before the one with lower voltage
 * @return CHARGER_EMUL_NON_FIRST_PDO_FIXED_FLAGS if PDO different than first
 *         has some flags set
 * @return CHARGER_EMUL_BATT_VOLT_REPEATED if two or more battery type PDOs have
 *         the same min and max voltage
 * @return CHARGER_EMUL_BATT_VOLT_NOT_IN_ORDER if battery PDO with higher
 *         voltage is before the one with lower voltage
 * @return CHARGER_EMUL_VAR_VOLT_REPEATED if two or more variable type PDOs have
 *         the same min and max voltage
 * @return CHARGER_EMUL_VAR_VOLT_NOT_IN_ORDER if variable PDO with higher
 *         voltage is before the one with lower voltage
 * @return CHARGER_EMUL_PDO_AFTER_ZERO if PDOs of different types are not in
 *         correct order (fixed, battery, variable) or non-zero PDO is placed
 *         after zero PDO
 */
enum check_pdos_res charger_emul_check_pdos(struct charger_emul_data *data);

/**
 * @}
 */

#endif /* __EMUL_CHARGER */
