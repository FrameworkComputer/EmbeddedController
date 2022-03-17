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

#include <drivers/emul.h>
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "usb_pd.h"

/**
 * @brief USB-C malfunctioning sink device emulator backend API
 * @defgroup tcpci_faulty_snk_emul USB-C malfunctioning sink device emulator
 * @{
 *
 * USB-C malfunctioning sink device emulator can be attached to TCPCI emulator.
 * It works as sink device, but it can be configured to not respond to source
 * capability message (by not sending GoodCRC or Request after GoodCRC).
 */

/** Structure describing malfunctioning sink emulator data */
struct tcpci_faulty_snk_emul_data {
	/* List of action to perform */
	struct k_fifo action_list;
};

/** Structure describing standalone malfunctioning device emulator */
struct tcpci_faulty_snk_emul {
	/** Common TCPCI partner data */
	struct tcpci_partner_data common_data;
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Malfunctioning sink emulator data */
	struct tcpci_faulty_snk_emul_data data;
	/** Sink emulator data */
	struct tcpci_snk_emul_data snk_data;
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
 * @brief Initialise USB-C malfunctioning sink device emulator. Need to be
 *        called before any other function that is using common_data.
 *
 * @param emul Pointer to USB-C malfunctioning sink device emulator
 */
void tcpci_faulty_snk_emul_init(struct tcpci_faulty_snk_emul *emul);

/**
 * @brief Initialise USB-C malfunctioning sink device data structure.
 *
 * @param data Pointer to USB-C malfunctioning sink device emulator data
 */
void tcpci_faulty_snk_emul_init_data(struct tcpci_faulty_snk_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI.
 *
 * @param snk_data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param ops Pointer to TCPCI partner emulator operations
 * @param tcpci_emul Pointer to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error
 */
int tcpci_faulty_snk_emul_connect_to_tcpci(
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct emul *tcpci_emul);

/**
 * @brief Handle SOP messages as TCPCI dual role device
 *
 * @param data Pointer to USB-C malfunctioning sink device emulator data
 * @param snk_data Pointer to USB-C sink device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param ops Pointer to TCPCI partner emulator operations
 * @param msg Pointer to received message
 *
 * @return TCPCI_PARTNER_COMMON_MSG_HANDLED Message was handled
 * @return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED Message wasn't handled
 */
enum tcpci_partner_handler_res tcpci_faulty_snk_emul_handle_sop_msg(
	struct tcpci_faulty_snk_emul_data *data,
	struct tcpci_snk_emul_data *snk_data,
	struct tcpci_partner_data *common_data,
	const struct tcpci_emul_partner_ops *ops,
	const struct tcpci_emul_msg *msg);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_FAULTY_SNK_H */
