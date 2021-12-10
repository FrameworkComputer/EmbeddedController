/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C source device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_SRC_H
#define __EMUL_TCPCI_PARTNER_SRC_H

#include <emul.h>
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/**
 * @brief USB-C source device emulator backend API
 * @defgroup tcpci_src_emul USB-C source device emulator
 * @{
 *
 * USB-C source device emulator can be attached to TCPCI emulator. It is able to
 * respond to some TCPM messages. It always attach as source and present
 * source capabilities constructed from given PDOs.
 */

/** Structure describing source device emulator */
struct tcpci_src_emul_data {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Power data objects returned in source capabilities message */
	uint32_t pdo[PDO_MAX_OBJECTS];
};

/** Return values of @ref tcpci_src_emul_check_pdos function */
enum check_pdos_res {
	TCPCI_SRC_EMUL_CHECK_PDO_OK = 0,
	TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V,
	TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED,
	TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS,
	TCPCI_SRC_EMUL_BATT_VOLT_REPEATED,
	TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_VAR_VOLT_REPEATED,
	TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_PDO_AFTER_ZERO,
};

/**
 * @brief Initialise USB-C source device emulator. Need to be called before
 *        any other function.
 *
 * @param data Pointer to USB-C source device emulator
 */
void tcpci_src_emul_init(struct tcpci_src_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI
 *
 * @param data Pointer to USB-C source device emulator
 * @param tcpci_emul Poinetr to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error or send source capabilities error
 */
int tcpci_src_emul_connect_to_tcpci(struct tcpci_src_emul_data *data,
				    const struct emul *tcpci_emul);

/**
 * @brief Check if PDOs of given source device emulator are in correct order
 *
 * @param data Pointer to USB-C source device emulator
 *
 * @return TCPCI_SRC_EMUL_CHECK_PDO_OK if PDOs are correct
 * @return TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V if first PDO is not
 *         fixed type 5V
 * @return TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED if two or more fixed type PDOs
 *         have the same voltage
 * @return TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER if fixed PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS if PDO different than first
 *         has some flags set
 * @return TCPCI_SRC_EMUL_BATT_VOLT_REPEATED if two or more battery type PDOs
 *         have the same min and max voltage
 * @return TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER if battery PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_VAR_VOLT_REPEATED if two or more variable type PDOs
 *         have the same min and max voltage
 * @return TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER if variable PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_PDO_AFTER_ZERO if PDOs of different types are not in
 *         correct order (fixed, battery, variable) or non-zero PDO is placed
 *         after zero PDO
 */
enum check_pdos_res tcpci_src_emul_check_pdos(struct tcpci_src_emul_data *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_SRC_H */
