/* Copyright 2022 The ChromiumOS Authors
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

#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "usb_pd.h"

#include <zephyr/drivers/emul.h>

/**
 * @brief USB-C dual role device extension backend API
 * @defgroup tcpci_snk_emul USB-C dual role device extension
 * @{
 *
 * USB-C DRP device emulator can be used with TCPCI partner emulator. It is able
 * to switch power role on PR SWAP message. It is required to provide sink and
 * source extensions to initialise the DRP extension. If sink or source
 * extension first capabilities PDO is changed after initialisation, function
 * @ref tcpci_drp_emul_set_dr_in_first_pdo should be called to select correct
 * flag specific for DRP device.
 */

/** Structure describing dual role device emulator data */
struct tcpci_drp_emul_data {
	/** Common extension structure */
	struct tcpci_partner_extension ext;
	/** Controls if device is sink or source */
	bool sink;
	/** If device is during power swap and is expecting PS_RDY message */
	bool in_pwr_swap;
	/** Initial power role that should be restored on hard reset */
	enum pd_power_role initial_power_role;
};

/**
 * @brief Initialise USB-C DRP device data structure
 *
 * @param data Pointer to USB-C DRP device emulator data
 * @param common_data Pointer to USB-C device emulator common data
 * @param power_role Default power role used by USB-C DRP device on connection
 * @param src_ext Pointer to source extension
 * @param sink_ext Pointer to sink extension
 *
 * @return Pointer to USB-C DRP extension
 */
struct tcpci_partner_extension *
tcpci_drp_emul_init(struct tcpci_drp_emul_data *data,
		    struct tcpci_partner_data *common_data,
		    enum pd_power_role power_role,
		    struct tcpci_partner_extension *src_ext,
		    struct tcpci_partner_extension *snk_ext);

/**
 * @brief Set correct flags for first capabilities PDO to indicate that this
 *        device is power swap capable.
 *
 * @param pdo capability entry to change
 */
void tcpci_drp_emul_set_dr_in_first_pdo(uint32_t *pdo);

/**
 * @brief Simulate an FRS signal to the connected port.
 * @param data Pointer to USB-C DRP device emulator data
 */
void tcpci_drp_emul_signal_frs(struct tcpci_partner_data *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_DRP_H */
