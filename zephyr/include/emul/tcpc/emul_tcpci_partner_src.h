/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for USB-C source device emulator
 */

#ifndef __EMUL_TCPCI_PARTNER_SRC_H
#define __EMUL_TCPCI_PARTNER_SRC_H

#include <zephyr/drivers/emul.h>
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci.h"
#include "usb_pd.h"

/**
 * @brief USB-C source device extension backend API
 * @defgroup tcpci_src_emul USB-C source device extension
 * @{
 *
 * USB-C source device extension can be used with TCPCI partner emulator. It is
 * able to respond to some TCPM messages. It always attach as source and present
 * source capabilities constructed from given PDOs.
 */

/** Structure describing source device emulator data */
struct tcpci_src_emul_data {
	/** Common extension structure */
	struct tcpci_partner_extension ext;
	/** Power data objects returned in source capabilities message */
	uint32_t pdo[PDO_MAX_OBJECTS];
	/** Pointer to common TCPCI partner data */
	struct tcpci_partner_data *common_data;
	/** Delayed work which is executed on SourceCapability timeout */
	struct k_work_delayable source_capability_timeout;
	/** Flag tracking if partner has received an Alert message */
	bool alert_received;
	/** Flag tracking if partner has received a Status message */
	bool status_received;
};

/** Return values of @ref tcpci_src_emul_check_pdos function */
enum check_pdos_res {
	TCPCI_SRC_EMUL_CHECK_PDO_OK = 0,
	TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V,
	TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED,
	TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS,
	TCPCI_SRC_EMUL_BATT_VOLT_REPEATED,
	TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_VAR_VOLT_REPEATED,
	TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER,
	TCPCI_SRC_EMUL_PDO_AFTER_ZERO,
};

/**
 * @brief Check if PDOs of given source device emulator are in correct order
 *
 * @param data Pointer to USB-C source device emulator
 *
 * @return TCPCI_SRC_EMUL_CHECK_PDO_OK if PDOs are correct
 * @return TCPCI_SRC_EMUL_FIRST_PDO_NO_FIXED_5V if first PDO is not
 *         fixed type 5V
 * @return TCPCI_SRC_EMUL_FIXED_VOLT_REPEATED if two or more fixed type PDOs
 *         have the same voltage
 * @return TCPCI_SRC_EMUL_FIXED_VOLT_NOT_IN_ORDER if fixed PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_NON_FIRST_PDO_FIXED_FLAGS if PDO different than first
 *         has some flags set
 * @return TCPCI_SRC_EMUL_BATT_VOLT_REPEATED if two or more battery type PDOs
 *         have the same min and max voltage
 * @return TCPCI_SRC_EMUL_BATT_VOLT_NOT_IN_ORDER if battery PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_VAR_VOLT_REPEATED if two or more variable type PDOs
 *         have the same min and max voltage
 * @return TCPCI_SRC_EMUL_VAR_VOLT_NOT_IN_ORDER if variable PDO with higher
 *         voltage is before the one with lower voltage
 * @return TCPCI_SRC_EMUL_PDO_AFTER_ZERO if PDOs of different types are not in
 *         correct order (fixed, battery, variable) or non-zero PDO is placed
 *         after zero PDO
 */
enum check_pdos_res tcpci_src_emul_check_pdos(struct tcpci_src_emul_data *data);

/**
 * @brief Initialise USB-C source device data structure. Single PDO 5V@3A is
 *        created with fixed unconstrained flag.
 *
 * @param data Pointer to USB-C source device emulator data
 * @param common_data Pointer to USB-C device emulator common data
 * @param ext Pointer to next USB-C emulator extension
 *
 * @return Pointer to USB-C source extension
 */
struct tcpci_partner_extension *
tcpci_src_emul_init(struct tcpci_src_emul_data *data,
		    struct tcpci_partner_data *common_data,
		    struct tcpci_partner_extension *ext);

/**
 * @brief Send capability message constructed from source device emulator PDOs
 *
 * @param data Pointer to USB-C source device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param delay Optional delay
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return TCPCI_EMUL_TX_FAILED when TCPCI is configured to not handle
 *                              messages of this type
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
int tcpci_src_emul_send_capability_msg(struct tcpci_src_emul_data *data,
				       struct tcpci_partner_data *common_data,
				       uint64_t delay);

/**
 * @brief Send capability message constructed from source device emulator PDOs.
 *        SourceCapability timer is started when message wasn't send
 *        successfully. Emulator will try to send source capability message
 *        again on timeout. Otherwise SenderResponse timer is started and
 *        emulator will wait for Request message.
 *
 * @param data Pointer to USB-C source device emulator data
 * @param common_data Pointer to common TCPCI partner data
 * @param delay Optional delay
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return -ENOMEM when there is no free memory for message
 * @return -EINVAL on TCPCI emulator add RX message error
 */
int tcpci_src_emul_send_capability_msg_with_timer(
	struct tcpci_src_emul_data *data,
	struct tcpci_partner_data *common_data, uint64_t delay);

/**
 * @brief Clear the alert received flag.
 *
 * @param data - pointer to source emulator partner data
 */
void tcpci_src_emul_clear_alert_received(struct tcpci_src_emul_data *data);

/**
 * @brief Clear the status received flag.
 *
 * @param data - pointer to source emulator partner data
 */
void tcpci_src_emul_clear_status_received(struct tcpci_src_emul_data *data);

/**
 * @}
 */

#endif /* __EMUL_TCPCI_PARTNER_SRC_H */
