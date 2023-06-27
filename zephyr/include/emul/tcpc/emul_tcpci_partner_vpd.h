/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C VCONN-powered device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_VPD_H
#define __EMUL_TCPCI_PARTNER_VPD_H

#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"

#include <zephyr/drivers/emul.h>

/**
 * @brief USB-C VCONN-powered device extension backend API
 * @defgroup tcpci_src_emul USB-C source device extension
 * @{
 */

struct tcpci_vpd_emul_data {
	/** Common extension structure */
	struct tcpci_partner_extension ext;
	/** Pointer to common TCPCI partner data */
	struct tcpci_partner_data *common_data;
	struct tcpci_faulty_ext_data fault_ext;
	struct tcpci_faulty_ext_action fault_actions[1];
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	bool charge_through_connected;
};

struct tcpci_partner_extension *
tcpci_vpd_emul_init(struct tcpci_vpd_emul_data *data,
		    struct tcpci_partner_data *common_data,
		    struct tcpci_partner_extension *ext);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_VPD_H */
