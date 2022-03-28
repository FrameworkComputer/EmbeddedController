/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C malfunctioning sink device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_FAULTY_SNK_H
#define __EMUL_TCPCI_PARTNER_FAULTY_SNK_H

#include <zephyr/drivers/emul.h>
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "usb_pd.h"

/**
 * @brief USB-C malfunctioning sink device extension backend API
 * @defgroup tcpci_faulty_snk_emul USB-C malfunctioning sink device extension
 * @{
 *
 * USB-C malfunctioning sink device extension can be used with TCPCI partner
 * emulator. It can be configured to not respond to source capability message
 * (by not sending GoodCRC or Request after GoodCRC).
 */

/** Structure describing malfunctioning sink emulator data */
struct tcpci_faulty_snk_emul_data {
	struct tcpci_partner_extension ext;
	/* List of action to perform */
	struct k_fifo action_list;
};

/** Actions that can be performed by malfunctioning sink emulator */
enum tcpci_faulty_snk_action_type {
	/**
	 * Fail to receive SourceCapabilities message. From TCPM point of view,
	 * GoodCRC message is not received.
	 */
	TCPCI_FAULTY_SNK_FAIL_SRC_CAP = BIT(0),
	/**
	 * Ignore to respond to SourceCapabilities message with Request message.
	 * From TCPM point of view, GoodCRC message is received, but Request is
	 * missing.
	 */
	TCPCI_FAULTY_SNK_IGNORE_SRC_CAP = BIT(1),
	/** Discard SourceCapabilities message and send Accept message */
	TCPCI_FAULTY_SNK_DISCARD_SRC_CAP = BIT(2),
};

/** Structure to put in malfunctioning sink emulator action list */
struct tcpci_faulty_snk_action {
	/* Reserved for FIFO */
	void *fifo_reserved;
	/* Actions that emulator should perform */
	uint32_t action_mask;
	/* Number of times to repeat action */
	int count;
};

/* Count of actions which is treated by emulator as infinite */
#define TCPCI_FAULTY_SNK_INFINITE_ACTION	0

/**
 * @brief Initialise USB-C malfunctioning sink device data structure
 *
 * @param data Pointer to USB-C malfunctioning sink device emulator data
 * @param common_data Pointer to USB-C device emulator common data
 * @param ext Pointer to next USB-C emulator extension
 *
 * @return Pointer to USB-C malfunctioning sink extension
 */
struct tcpci_partner_extension *tcpci_faulty_snk_emul_init(
	struct tcpci_faulty_snk_emul_data *data,
	struct tcpci_partner_data *common_data,
	struct tcpci_partner_extension *ext);

/**
 * @brief Add action to perform by USB-C malfunctioning sink extension
 *
 * @param data Pointer to USB-C malfunctioning sink device extension data
 * @param action Non standard behavior to perform by emulator
 */
void tcpci_faulty_snk_emul_append_action(
	struct tcpci_faulty_snk_emul_data *data,
	struct tcpci_faulty_snk_action *action);

/**
 * @brief Clear all actions of USB-C malfunctioning sink extension
 *
 * @param data Pointer to USB-C malfunctioning sink device extension data
 */
void tcpci_faulty_snk_emul_clear_actions_list(
	struct tcpci_faulty_snk_emul_data *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_FAULTY_SNK_H */
