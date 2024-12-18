/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C malfunctioning device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_FAULTY_EXT_H
#define __EMUL_TCPCI_PARTNER_FAULTY_EXT_H

#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "usb_pd.h"

#include <zephyr/drivers/emul.h>

/**
 * @brief USB-C malfunctioning device extension backend API
 * @defgroup tcpci_faulty_ext USB-C malfunctioning device extension
 * @{
 *
 * USB-C malfunctioning device extension can be used with TCPCI partner
 * emulator. It can be configured to not respond to source capability message
 * (by not sending GoodCRC or Request after GoodCRC).
 */

/** Structure describing malfunctioning emulator data */
struct tcpci_faulty_ext_data {
	struct tcpci_partner_extension ext;
	/* List of action to perform */
	struct k_fifo action_list;
};

/** Actions that can be performed by malfunctioning emulator */
enum tcpci_faulty_ext_action_type {
	/**
	 * Fail to receive SourceCapabilities message. From TCPM point of view,
	 * GoodCRC message is not received.
	 */
	TCPCI_FAULTY_EXT_FAIL_SRC_CAP = BIT(0),
	/**
	 * Ignore to respond to SourceCapabilities message with Request message.
	 * From TCPM point of view, GoodCRC message is received, but Request is
	 * missing.
	 */
	TCPCI_FAULTY_EXT_IGNORE_SRC_CAP = BIT(1),
	/** Discard SourceCapabilities message and send Accept message */
	TCPCI_FAULTY_EXT_DISCARD_SRC_CAP = BIT(2),
	/** Ignore FR_Swap message and do not respond */
	TCPCI_FAULTY_EXT_IGNORE_FR_SWAP = BIT(3),
	/** Reject FR_Swap message */
	TCPCI_FAULTY_EXT_REJECT_FR_SWAP = BIT(4),
	/** Accept FR_Swap message but violate tSenderResponse */
	TCPCI_FAULTY_EXT_ACCEPT_FR_SWAP_TIMEOUT = BIT(5),
};

/** Structure to put in malfunctioning emulator action list */
struct tcpci_faulty_ext_action {
	/* Reserved for FIFO */
	void *fifo_reserved;
	/* Actions that emulator should perform */
	uint32_t action_mask;
	/* Number of times to repeat action */
	int count;
};

/* Count of actions which is treated by emulator as infinite */
#define TCPCI_FAULTY_EXT_INFINITE_ACTION 0

/**
 * @brief Initialise USB-C malfunctioning device data structure
 *
 * @param data Pointer to USB-C malfunctioning device emulator data
 * @param common_data Pointer to USB-C device emulator common data
 * @param ext Pointer to next USB-C emulator extension
 *
 * @return Pointer to USB-C malfunctioning extension
 */
struct tcpci_partner_extension *
tcpci_faulty_ext_init(struct tcpci_faulty_ext_data *data,
		      struct tcpci_partner_data *common_data,
		      struct tcpci_partner_extension *ext);

/**
 * @brief Add action to perform by USB-C malfunctioning extension
 *
 * @param data Pointer to USB-C malfunctioning device extension data
 * @param action Non standard behavior to perform by emulator
 */
void tcpci_faulty_ext_append_action(struct tcpci_faulty_ext_data *data,
				    struct tcpci_faulty_ext_action *action);

/**
 * @brief Clear all actions of USB-C malfunctioning extension
 *
 * @param data Pointer to USB-C malfunctioning device extension data
 */
void tcpci_faulty_ext_clear_actions_list(struct tcpci_faulty_ext_data *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_FAULTY_EXT_H */
