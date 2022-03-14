/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for TCPCI emulator
 */

#ifndef __EMUL_TCPCI_H
#define __EMUL_TCPCI_H

#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <usb_pd_tcpm.h>

/**
 * @brief TCPCI emulator backend API
 * @defgroup tcpci_emul TCPCI emulator
 * @{
 *
 * TCPCI emulator supports access to its registers using I2C messages.
 * It follows Type-C Port Controller Interface Specification. It is possible
 * to use this emulator as base for implementation of specific TCPC emulator
 * which follows TCPCI specification. Emulator allows to set callbacks
 * on change of CC status or transmitting message to implement partner emulator.
 * There is also callback used to inform about alert line state change.
 * Application may alter emulator state:
 *
 * - call @ref tcpci_emul_set_reg and @ref tcpci_emul_get_reg to set and get
 *   value of TCPCI registers
 * - call functions from emul_common_i2c.h to setup custom handlers for I2C
 *   messages
 * - call @ref tcpci_emul_add_rx_msg to setup received SOP messages
 * - call @ref tcpci_emul_get_tx_msg to examine sended message
 * - call @ref tcpci_emul_set_rev to set revision of emulated TCPCI
 */

/** SOP message structure */
struct tcpci_emul_msg {
	/** Pointer to buffer for header and message */
	uint8_t *buf;
	/** Number of bytes in buf */
	int cnt;
	/** Type of message (SOP, SOP', etc) */
	uint8_t type;
	/** Index used to mark accessed byte */
	int idx;
	/** Pointer to optional second message */
	struct tcpci_emul_msg *next;
};

/**
 * @brief Function type that is used by TCPCI emulator to provide information
 *        about alert line state
 *
 * @param emul Pointer to emulator
 * @param alert State of alert line (false - low, true - high)
 * @param data Pointer to custom function data
 */
typedef void (*tcpci_emul_alert_state_func)(const struct emul *emul, bool alert,
					    void *data);

/** Response from TCPCI specific device operations */
enum tcpci_emul_ops_resp {
	TCPCI_EMUL_CONTINUE = 0,
	TCPCI_EMUL_DONE,
	TCPCI_EMUL_ERROR
};

/** Revisions supported by TCPCI emaluator */
enum tcpci_emul_rev {
	TCPCI_EMUL_REV1_0_VER1_0 = 0,
	TCPCI_EMUL_REV2_0_VER1_1
};

/** Status of TX message send to TCPCI emulator partner */
enum tcpci_emul_tx_status {
	TCPCI_EMUL_TX_SUCCESS,
	TCPCI_EMUL_TX_DISCARDED,
	TCPCI_EMUL_TX_FAILED,
	TCPCI_EMUL_TX_CABLE_HARD_RESET
};

/** TCPCI specific device operations. Not all of them need to be implemented. */
struct tcpci_emul_dev_ops {
	/**
	 * @brief Function called for each byte of read message
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to device operations structure
	 * @param reg First byte of last write message
	 * @param val Pointer where byte to read should be stored
	 * @param bytes Number of bytes already readded
	 *
	 * @return TCPCI_EMUL_CONTINUE to continue with default handler
	 * @return TCPCI_EMUL_DONE to immedietly return success
	 * @return TCPCI_EMUL_ERROR to immedietly return error
	 */
	enum tcpci_emul_ops_resp (*read_byte)(const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, uint8_t *val, int bytes);

	/**
	 * @brief Function called for each byte of write message
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to device operations structure
	 * @param reg First byte of write message
	 * @param val Received byte of write message
	 * @param bytes Number of bytes already received
	 *
	 * @return TCPCI_EMUL_CONTINUE to continue with default handler
	 * @return TCPCI_EMUL_DONE to immedietly return success
	 * @return TCPCI_EMUL_ERROR to immedietly return error
	 */
	enum tcpci_emul_ops_resp (*write_byte)(const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, uint8_t val, int bytes);

	/**
	 * @brief Function called on the end of write message
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to device operations structure
	 * @param reg Register which is written
	 * @param msg_len Length of handled I2C message
	 *
	 * @return TCPCI_EMUL_CONTINUE to continue with default handler
	 * @return TCPCI_EMUL_DONE to immedietly return success
	 * @return TCPCI_EMUL_ERROR to immedietly return error
	 */
	enum tcpci_emul_ops_resp (*handle_write)(const struct emul *emul,
					const struct tcpci_emul_dev_ops *ops,
					int reg, int msg_len);

	/**
	 * @brief Function called on reset
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to device operations structure
	 */
	void (*reset)(const struct emul *emul, struct tcpci_emul_dev_ops *ops);
};

/** TCPCI partner operations. Not all of them need to be implemented. */
struct tcpci_emul_partner_ops {
	/**
	 * @brief Function called when TCPM wants to transmit message to partner
	 *        connected to TCPCI
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to partner operations structure
	 * @param tx_msg Pointer to TX message buffer
	 * @param type Type of message
	 * @param retry Count of retries
	 */
	void (*transmit)(const struct emul *emul,
			 const struct tcpci_emul_partner_ops *ops,
			 const struct tcpci_emul_msg *tx_msg,
			 enum tcpci_msg_type type,
			 int retry);

	/**
	 * @brief Function called when control settings change to allow partner
	 *        to react
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to partner operations structure
	 */
	void (*control_change)(const struct emul *emul,
			       const struct tcpci_emul_partner_ops *ops);

	/**
	 * @brief Function called when TCPM consumes message send by partner
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to partner operations structure
	 * @param rx_msg Message that was consumed by TCPM
	 */
	void (*rx_consumed)(const struct emul *emul,
			    const struct tcpci_emul_partner_ops *ops,
			    const struct tcpci_emul_msg *rx_msg);

	/**
	 * @brief Function called when partner is disconnected from TCPCI
	 *
	 * @param emul Pointer to TCPCI emulator
	 * @param ops Pointer to partner operations structure
	 */
	void (*disconnect)(const struct emul *emul,
			   const struct tcpci_emul_partner_ops *ops);
};

/**
 * @brief Get i2c_emul for TCPCI emulator
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return Pointer to I2C TCPCI emulator
 */
struct i2c_emul *tcpci_emul_get_i2c_emul(const struct emul *emul);

/**
 * @brief Set value of given register of TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 */
int tcpci_emul_set_reg(const struct emul *emul, int reg, uint16_t val);

/**
 * @brief Get value of given register of TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 * @param reg Register address
 * @param val Pointer where value should be stored
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 *                 or val is NULL
 */
int tcpci_emul_get_reg(const struct emul *emul, int reg, uint16_t *val);

/**
 * @brief Add up to two SOP RX messages
 *
 * @param emul Pointer to TCPCI emulator
 * @param rx_msg Pointer to message that is added
 * @param alert Select if alert register should be updated
 *
 * @return 0 on success
 * @return -EINVAL on error (too long message or adding third message)
 */
int tcpci_emul_add_rx_msg(const struct emul *emul,
			  struct tcpci_emul_msg *rx_msg, bool alert);

/**
 * @brief Get SOP TX message to examine what was sended by TCPM
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return Pointer to TX message
 */
struct tcpci_emul_msg *tcpci_emul_get_tx_msg(const struct emul *emul);

/**
 * @brief Set TCPCI revision in PD_INT_REV register
 *
 * @param emul Pointer to TCPCI emulator
 * @param rev Requested revision
 */
void tcpci_emul_set_rev(const struct emul *emul, enum tcpci_emul_rev rev);

/**
 * @brief Set callbacks for specific TCPC device emulator
 *
 * @param emul Pointer to TCPCI emulator
 * @param dev_ops Pointer to callbacks
 */
void tcpci_emul_set_dev_ops(const struct emul *emul,
			    struct tcpci_emul_dev_ops *dev_ops);

/**
 * @brief Set callback which is called when alert register is changed
 *
 * @param emul Pointer to TCPCI emulator
 * @param alert_callback Pointer to callback
 * @param alert_callback_data Pointer to data passed to callback as an argument
 */
void tcpci_emul_set_alert_callback(const struct emul *emul,
				   tcpci_emul_alert_state_func alert_callback,
				   void *alert_callback_data);

/**
 * @brief Set callbacks for port partner device emulator
 *
 * @param emul Pointer to TCPCI emulator
 * @param partner Pointer to callbacks
 */
void tcpci_emul_set_partner_ops(const struct emul *emul,
				const struct tcpci_emul_partner_ops *partner);

/**
 * @brief Emulate connection of specific device to emulated TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 * @param partner_power_role Power role of connected partner (sink or source)
 * @param partner_cc1 Voltage on partner CC1 line (usually Rd or Rp)
 * @param partner_cc2 Voltage on partner CC2 line (usually open or Ra if active
 *                    cable is emulated)
 * @param polarity Polarity of plug. If POLARITY_CC1 then partner_cc1 is
 *                 connected to TCPCI CC1 line. Otherwise partner_cc1 is
 *                 connected to TCPCI CC2 line.
 *
 * @return 0 on success
 * @return negative on error
 */
int tcpci_emul_connect_partner(const struct emul *emul,
			       enum pd_power_role partner_power_role,
			       enum tcpc_cc_voltage_status partner_cc1,
			       enum tcpc_cc_voltage_status partner_cc2,
			       enum tcpc_cc_polarity polarity);

/** @brief Emulate the disconnection of the partner device to emulated TCPCI
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return 0 on success
 */
int tcpci_emul_disconnect_partner(const struct emul *emul);

/**
 * @brief Allows port partner to select if message was received correctly
 *
 * @param emul Pointer to TCPCI emulator
 * @param status Status of sended message
 */
void tcpci_emul_partner_msg_status(const struct emul *emul,
				   enum tcpci_emul_tx_status status);

/**
 * @}
 */

#endif /* __EMUL_TCPCI */
