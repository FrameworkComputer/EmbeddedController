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

#include <drivers/emul.h>
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

/** Structure describing sink device emulator data */
struct tcpci_snk_emul_data {
	/** Power data objects returned in sink capabilities message */
	uint32_t pdo[PDO_MAX_OBJECTS];
	/** Emulator is waiting for PS RDY message */
	bool wait_for_ps_rdy;
	/** PS RDY was received and PD negotiation is completed */
	bool pd_completed;
};

/** Structure describing standalone sink device emulator */
struct tcpci_snk_emul {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Sink emulator data */
	struct tcpci_snk_emul_data data;
};

/**
 * @brief Initialise USB-C sink device emulator. Need to be called before
 *        any other function that is using common_data.
 *
 * @param emul Pointer to USB-C sink device emulator
 */
void tcpci_snk_emul_init(struct tcpci_snk_emul *emul);

/**
 * @brief Initialise USB-C sink device data structure. Single PDO 5V@500mA is
 *        created and all flags are cleared.
 *
 * @param data Pointer to USB-C sink device emulator data
 */
void tcpci_snk_emul_init_data(struct tcpci_snk_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI
 *
 * @param data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param ops Pointer to TCPCI partner emulator operations
 * @param tcpci_emul Pointer to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
int tcpci_snk_emul_connect_to_tcpci(struct tcpci_snk_emul_data *data,
				    struct tcpci_partner_data *common_data,
				    const struct tcpci_emul_partner_ops *ops,
				    const struct emul *tcpci_emul);

/**
 * @brief Handle SOP messages as TCPCI sink device. It handles source cap,
 *        get sink cap and ping messages. Accept, Reject and PS_RDY are handled
 *        only if sink emulator send request as response for source cap message
 *        and is waiting for response.
 *
 * @param data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param msg Pointer to received message
 *
 * @param TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled
 * @param TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 */
enum tcpci_partner_handler_res tcpci_snk_emul_handle_sop_msg(
	struct tcpci_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_msg *msg);

/**
 * @brief Perform action required by sink device on hard reset. Reset sink
 *        specific flags (pd_completed and wait_for_ps_rdy).
 *
 * @param data Pointer to USB-C source device emulator data
 */
void tcpci_snk_emul_hard_reset(void *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_SNK_H */
