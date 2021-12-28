/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C sink device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_SNK_H
#define __EMUL_TCPCI_PARTNER_SNK_H

#include <emul.h>
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/**
 * @brief USB-C sink device emulator backend API
 * @defgroup tcpci_snk_emul USB-C sink device emulator
 * @{
 *
 * USB-C sink device emulator can be attached to TCPCI emulator. It is able to
 * respond to some TCPM messages. It always attach as sink and present
 * sink capabilities constructed from given PDOs.
 */

/** Structure describing sink device emulator */
struct tcpci_snk_emul_data {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Power data objects returned in sink capabilities message */
	uint32_t pdo[PDO_MAX_OBJECTS];
	/** Emulator is waiting for PS RDY message */
	bool wait_for_ps_rdy;
	/** PS RDY was received and PD negotiation is completed */
	bool pd_completed;
};

/**
 * @brief Initialise USB-C sink device emulator. Need to be called before
 *        any other function.
 *
 * @param data Pointer to USB-C sink device emulator
 */

void tcpci_snk_emul_init(struct tcpci_snk_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI
 *
 * @param data Pointer to USB-C sink device emulator
 * @param tcpci_emul Pointer to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
int tcpci_snk_emul_connect_to_tcpci(struct tcpci_snk_emul_data *data,
				    const struct emul *tcpci_emul);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_SNK_H */
