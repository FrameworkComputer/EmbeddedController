/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C charger emulator
 */

#ifndef __EMUL_CHARGER_H
#define __EMUL_CHARGER_H

#include <emul.h>
#include "emul/emul_tcpci.h"

/**
 * @brief USB-C charger emulator backend API
 * @defgroup charger_emul USB-C charger emulator
 * @{
 *
 * USB-C charger emulator can be attached to TCPCI emulator. It is able to
 * respond to some TCPM messages. It always attach as source and present
 * hardcoded set of source capabilities.
 */

/** Structure describing charger emulator */
struct charger_emul_data {
	/** Operations used by TCPCI emulator */
	struct tcpci_emul_partner_ops ops;
	/** Work used to send message with delay */
	struct k_work_delayable delayed_send;
	/** Pointer to connected TCPCI emulator */
	const struct emul *tcpci_emul;
	/** Queue for delayed messages */
	struct k_fifo to_send;
	/** Next SOP message id */
	int msg_id;
};

/**
 * @brief Initialise USB-C charger emulator. Need to be called before any other
 *        function.
 *
 * @param data Pointer to USB-C charger emulator
 */
void charger_emul_init(struct charger_emul_data *data);

/**
 * @brief Connect emulated device to TCPCI
 *
 * @param data Pointer to USB-C charger emulator
 * @param tcpci_emul Poinetr to TCPCI emulator to connect
 *
 * @return 0 on success
 * @return negative on TCPCI connect error or send source capabilities error
 */
int charger_emul_connect_to_tcpci(struct charger_emul_data *data,
				  const struct emul *tcpci_emul);

/**
 * @}
 */

#endif /* __EMUL_CHARGER */
