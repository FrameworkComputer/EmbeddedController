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

#include <zephyr/drivers/emul.h>
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/**
 * @brief USB-C sink device extension backend API
 * @defgroup tcpci_snk_emul USB-C sink device extension
 * @{
 *
 * USB-C sink device extension can be used with TCPCI partner emulator. It is
 * able to respond to some TCPM messages. It always attach as sink and present
 * sink capabilities constructed from given PDOs.
 */

/** Structure describing sink device emulator data */
struct tcpci_snk_emul_data {
	/** Common extension structure */
	struct tcpci_partner_extension ext;
	/** Power data objects returned in sink capabilities message */
	uint32_t pdo[PDO_MAX_OBJECTS];
	/** Emulator is waiting for PS RDY message */
	bool wait_for_ps_rdy;
	/** PS RDY was received and PD negotiation is completed */
	bool pd_completed;
	/** PD_CTRL_PING message received  */
	bool ping_received;
	/** PD_DATA_ALERT message received  */
	bool alert_received;
};

/**
 * @brief Initialise USB-C sink device data structure. Single PDO 5V@500mA is
 *        created and all flags are cleared.
 *
 * @param data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to USB-C device emulator common data
 * @param ext Pointer to next USB-C emulator extension
 *
 * @return Pointer to USB-C sink extension
 */
struct tcpci_partner_extension *tcpci_snk_emul_init(
	struct tcpci_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	struct tcpci_partner_extension *ext);

/**
 * @brief Clear the ping received flag.
 *
 * @param sink_data
 */
void tcpci_snk_emul_clear_ping_received(struct tcpci_snk_emul_data *sink_data);

/**
 * @brief Clear the alert received flag.
 *
 * @param sink_data
 */
void tcpci_snk_emul_clear_alert_received(struct tcpci_snk_emul_data *sink_data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_SNK_H */
