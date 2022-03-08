/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C dual role device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_DRP_H
#define __EMUL_TCPCI_PARTNER_DRP_H

#include <drivers/emul.h>
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "usb_pd.h"

/**
 * @brief USB-C dual role device emulator backend API
 * @defgroup tcpci_snk_emul USB-C dual role device emulator
 * @{
 *
 * USB-C DRP device emulator can be attached to TCPCI emulator as sink or
 * source device. It is able to switch power role on PR SWAP message. It is able
 * to send both source and sink capabilities.
 */

/** Structure describing dual role device emulator data */
struct tcpci_drp_emul_data {
	/** Controls if device is sink or source */
	bool sink;
	/** If device is during power swap and is expecting PS_RDY message */
	bool in_pwr_swap;
};

/** Structure describing standalone dual role device emulator */
struct tcpci_drp_emul {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Dual role emulator data */
	struct tcpci_drp_emul_data data;
	/** Source emulator data */
	struct tcpci_src_emul_data src_data;
	/** Sink emulator data */
	struct tcpci_snk_emul_data snk_data;
};

/**
 * @brief Initialise USB-C dual role device emulator. Need to be called before
 *        any other function that is using common_data.
 *
 * @param emul Pointer to USB-C dual role device emulator
 */
void tcpci_drp_emul_init(struct tcpci_drp_emul *emul);

/**
 * @brief Connect emulated device to TCPCI. Connect as sink or source depending
 *        on sink field in @p data structure. @p common_data power_role field
 *        should be set correctly before calling this function.
 *
 * @param data Pointer to USB-C dual role device emulator data
 * @param src_data Pointer to USB-C source device emulator data
 * @param snk_data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param ops Pointer to TCPCI partner emulator operations
 * @param tcpci_emul Pointer to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
int tcpci_drp_emul_connect_to_tcpci(struct tcpci_drp_emul_data *data,
				    struct tcpci_src_emul_data *src_data,
				    struct tcpci_snk_emul_data *snk_data,
				    struct tcpci_partner_data *common_data,
				    const struct tcpci_emul_partner_ops *ops,
				    const struct emul *tcpci_emul);

/**
 * @brief Handle SOP messages as TCPCI dual role device
 *
 * @param data Pointer to USB-C dual role device emulator data
 * @param src_data Pointer to USB-C source device emulator data
 * @param snk_data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param ops Pointer to TCPCI partner emulator operations
 * @param msg Pointer to received message
 *
 * @return TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled
 * @return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 */
enum tcpci_partner_handler_res tcpci_drp_emul_handle_sop_msg(
	struct tcpci_drp_emul_data *data,
	struct tcpci_src_emul_data *src_data,
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct tcpci_emul_msg *msg);

/**
 * @brief Perform action required by DRP device on hard reset. If device acts
 *        as sink, sink hard reset function is called. Otherwise source hard
 *        reset function is called.
 *
 * @param data Pointer to USB-C dual role device emulator data
 */
void tcpci_src_emul_hard_reset(void *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_DRP_H */
