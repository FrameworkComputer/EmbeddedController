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

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <usb_pd_tcpm.h>

#include "emul/emul_common_i2c.h"

/**
 * Number of emulated register. This include vendor registers defined in TCPCI
 * specification
 */
#define TCPCI_EMUL_REG_COUNT 0x100

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

/** Run-time data used by the emulator */
struct tcpci_ctx {
	/** Common I2C data for TCPC */
	struct i2c_common_emul_data common;

	/** Current state of all emulated TCPCI registers */
	uint8_t reg[TCPCI_EMUL_REG_COUNT];

	/** Structures representing TX and RX buffers */
	struct tcpci_emul_msg *rx_msg;
	struct tcpci_emul_msg *tx_msg;

	/** Data that should be written to register (except TX_BUFFER) */
	uint16_t write_data;

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;

	/** User function called when alert line could change */
	tcpci_emul_alert_state_func alert_callback;
	/** Data passed to alert_callback */
	void *alert_callback_data;

	/** Callbacks for TCPCI partner */
	const struct tcpci_emul_partner_ops *partner;

	/** Reference to Alert# GPIO emulator. */
	const struct device *alert_gpio_port;
	gpio_pin_t alert_gpio_pin;
};

/** Run-time data used by the emulator */
struct tcpc_emul_data {
	/** Pointer to the common TCPCI emulator context */
	struct tcpci_ctx *tcpci_ctx;

	/** Pointer to chip specific data */
	void *chip_data;

	const struct i2c_common_emul_cfg i2c_cfg;
};

#define TCPCI_EMUL_DEFINE(n, init, cfg_ptr, chip_data_ptr)                   \
	static uint8_t tcpci_emul_tx_buf_##n[128];                           \
	static struct tcpci_emul_msg tcpci_emul_tx_msg_##n = {               \
		.buf = tcpci_emul_tx_buf_##n,                                \
	};                                                                   \
	static struct tcpci_ctx tcpci_ctx##n = {                             \
		.tx_msg = &tcpci_emul_tx_msg_##n,                            \
		.error_on_ro_write = true,                                   \
		.error_on_rsvd_write = true,                                 \
		.alert_gpio_port = COND_CODE_1(                              \
			DT_INST_NODE_HAS_PROP(n, alert_gpio),                \
			(DEVICE_DT_GET(DT_GPIO_CTLR(                         \
				DT_INST_PROP(n, alert_gpio), gpios))),       \
			(NULL)),                                             \
		.alert_gpio_pin = COND_CODE_1(                               \
			DT_INST_NODE_HAS_PROP(n, alert_gpio),                \
			(DT_GPIO_PIN(DT_INST_PROP(n, alert_gpio), gpios)),   \
			(0)),                                                \
	};                                                                   \
	static struct tcpc_emul_data tcpc_emul_data_##n = {                \
		.tcpci_ctx = &tcpci_ctx##n,                                \
		.chip_data = chip_data_ptr,                                \
		.i2c_cfg = {                                               \
			.i2c_label = DT_INST_BUS_LABEL(n),                 \
			.dev_label = DT_INST_LABEL(n),                     \
			.data = &tcpci_ctx##n.common,                      \
			.addr = DT_INST_REG_ADDR(n),                       \
		},                                                         \
	}; \
	EMUL_DEFINE(init, DT_DRV_INST(n), cfg_ptr, &tcpc_emul_data_##n)

/** Response from TCPCI specific device operations */
enum tcpci_emul_ops_resp {
	TCPCI_EMUL_CONTINUE = 0,
	TCPCI_EMUL_DONE,
	TCPCI_EMUL_ERROR
};

/** Revisions supported by TCPCI emaluator */
enum tcpci_emul_rev { TCPCI_EMUL_REV1_0_VER1_0 = 0, TCPCI_EMUL_REV2_0_VER1_1 };

/** Status of TX message send to TCPCI emulator partner */
enum tcpci_emul_tx_status {
	TCPCI_EMUL_TX_SUCCESS = 0,
	TCPCI_EMUL_TX_DISCARDED,
	TCPCI_EMUL_TX_FAILED,
	TCPCI_EMUL_TX_CABLE_HARD_RESET,
	/*
	 * This is not real status. It is used to log unusual situation outside
	 * the TCPCI spec.
	 */
	TCPCI_EMUL_TX_UNKNOWN
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
	enum tcpci_emul_ops_resp (*read_byte)(
		const struct emul *emul, const struct tcpci_emul_dev_ops *ops,
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
	enum tcpci_emul_ops_resp (*write_byte)(
		const struct emul *emul, const struct tcpci_emul_dev_ops *ops,
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
	enum tcpci_emul_ops_resp (*handle_write)(
		const struct emul *emul, const struct tcpci_emul_dev_ops *ops,
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
			 enum tcpci_msg_type type, int retry);

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
 * @param emul Pointer to TCPC emulator
 *
 * @return Pointer to I2C TCPCI emulator
 */
struct i2c_emul *tcpci_emul_get_i2c_emul(const struct emul *emul);

/**
 * @brief Set value of given register of TCPCI
 *
 * @param emul Pointer to TCPC emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 */
int tcpci_emul_set_reg(const struct emul *emul, int reg, uint16_t val);

/**
 * @brief Function called for each byte of read message from TCPCI
 *
 * @param emul Pointer to TCPC emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readded
 *
 * @return 0 on success
 */
int tcpci_emul_read_byte(const struct emul *emul, int reg, uint8_t *val,
			 int bytes);

/**
 * @brief Function called for each byte of write message from TCPCI.
 *        Data are stored in write_data field of tcpci_emul_data or in tx_msg
 *        in case of writing to TX buffer.
 *
 * @param emul Pointer to TCPC emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write to TX buffer
 */
int tcpci_emul_write_byte(const struct emul *emul, int reg, uint8_t val,
			  int bytes);

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0.
 *
 * @param emul Pointer to TCPC emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
int tcpci_emul_handle_write(const struct emul *emul, int reg, int msg_len);

/**
 * @brief Set up a new TCPCI emulator
 *
 * This should be called for each TCPC device that needs to be
 * registered on the I2C bus.
 *
 * @param emul Pointer to TCPC emulator
 * @param parent Pointer to emulated I2C bus
 */
void tcpci_emul_i2c_init(const struct emul *emul, const struct device *i2c_dev);

/**
 * @brief Reset registers to default values. Vendor and reserved registers
 *        are not changed.
 *
 * @param emul Pointer to TCPC emulator
 * @return 0 if successful
 */
int tcpci_emul_reset(const struct emul *emul);

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
 * @param emul Pointer to TCPC emulator
 * @param rx_msg Pointer to message that is added
 * @param alert Select if alert register should be updated
 *
 * @return TCPCI_EMUL_TX_SUCCESS on success
 * @return TCPCI_EMUL_TX_FAILED when TCPCI is configured to not handle
 *                              messages of this type
 * @return -EINVAL on error (too long message, adding third message or wrong
 *                 message type)
 * @return -EBUSY Failed to lock mutex for TCPCI emulator
 * @return -EAGAIN Failed to lock mutex for TCPCI emulator
 */
int tcpci_emul_add_rx_msg(const struct emul *emul,
			  struct tcpci_emul_msg *rx_msg, bool alert);

/**
 * @brief Get SOP TX message to examine what was sended by TCPM
 *
 * @param emul Pointer to TCPC emulator
 *
 * @return Pointer to TX message
 */
struct tcpci_emul_msg *tcpci_emul_get_tx_msg(const struct emul *emul);

/**
 * @brief Set TCPCI revision in PD_INT_REV register
 *
 * @param emul Pointer to TCPC emulator
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
 * @param emul Pointer to TCPC emulator
 * @param alert_callback Pointer to callback
 * @param alert_callback_data Pointer to data passed to callback as an argument
 */
void tcpci_emul_set_alert_callback(const struct emul *emul,
				   tcpci_emul_alert_state_func alert_callback,
				   void *alert_callback_data);

/**
 * @brief Set callbacks for port partner device emulator
 *
 * @param emul Pointer to TCPC emulator
 * @param partner Pointer to callbacks
 */
void tcpci_emul_set_partner_ops(const struct emul *emul,
				const struct tcpci_emul_partner_ops *partner);

/**
 * @brief Emulate connection of specific device to emulated TCPCI
 *
 * @param emul Pointer to TCPC emulator
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
 * @param emul Pointer to TCPC emulator
 *
 * @return 0 on success
 */
int tcpci_emul_disconnect_partner(const struct emul *emul);

/**
 * @brief Allows port partner to select if message was received correctly
 *
 * @param emul Pointer to TCPC emulator
 * @param status Status of sended message
 */
void tcpci_emul_partner_msg_status(const struct emul *emul,
				   enum tcpci_emul_tx_status status);

/**
 * @}
 */

#endif /* __EMUL_TCPCI */
