/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management - common header for TCPM drivers */

#ifndef __CROS_EC_USB_PD_TCPM_TCPM_H
#define __CROS_EC_USB_PD_TCPM_TCPM_H

#include "common.h"
#include "ec_commands.h"
#include "gpio.h"
#include "i2c.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#if defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) && \
	!defined(CONFIG_USB_PD_DUAL_ROLE)
#error "DRP auto toggle requires board to have DRP support"
#error "Please upgrade your board configuration"
#endif

#ifndef CONFIG_USB_PD_TCPC

/* I2C wrapper functions - get I2C port / slave addr from config struct. */
#ifndef CONFIG_USB_PD_TCPC_LOW_POWER
static inline int tcpc_addr_write(int port, int i2c_addr, int reg, int val)
{
	return i2c_write8(tcpc_config[port].i2c_info.port,
			  i2c_addr, reg, val);
}

static inline int tcpc_addr_write16(int port, int i2c_addr, int reg, int val)
{
	return i2c_write16(tcpc_config[port].i2c_info.port,
			   i2c_addr, reg, val);
}

static inline int tcpc_addr_read(int port, int i2c_addr, int reg, int *val)
{
	return i2c_read8(tcpc_config[port].i2c_info.port,
			 i2c_addr, reg, val);
}

static inline int tcpc_addr_read16(int port, int i2c_addr, int reg, int *val)
{
	return i2c_read16(tcpc_config[port].i2c_info.port,
			  i2c_addr, reg, val);
}

static inline int tcpc_xfer(int port, const uint8_t *out, int out_size,
			    uint8_t *in, int in_size)
{
	return i2c_xfer(tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			out, out_size, in, in_size);
}

static inline int tcpc_xfer_unlocked(int port, const uint8_t *out, int out_size,
			    uint8_t *in, int in_size, int flags)
{
	return i2c_xfer_unlocked(tcpc_config[port].i2c_info.port,
				 tcpc_config[port].i2c_info.addr_flags,
				 out, out_size, in, in_size, flags);
}

static inline int tcpc_read_block(int port, int reg, uint8_t *in, int size)
{
	return i2c_read_block(tcpc_config[port].i2c_info.port,
			      tcpc_config[port].i2c_info.addr_flags,
			      reg, in, size);
}

static inline int tcpc_write_block(int port, int reg,
		const uint8_t *out, int size)
{
	return i2c_write_block(tcpc_config[port].i2c_info.port,
			       tcpc_config[port].i2c_info.addr_flags,
			       reg, out, size);
}

static inline int tcpc_update8(int port, int reg,
			       uint8_t mask,
			       enum mask_update_action action)
{
	return i2c_update8(tcpc_config[port].i2c_info.port,
			   tcpc_config[port].i2c_info.addr_flags,
			   reg, mask, action);
}

static inline int tcpc_update16(int port, int reg,
				uint16_t mask,
				enum mask_update_action action)
{
	return i2c_update16(tcpc_config[port].i2c_info.port,
			    tcpc_config[port].i2c_info.addr_flags,
			    reg, mask, action);
}

#else /* !CONFIG_USB_PD_TCPC_LOW_POWER */
int tcpc_addr_write(int port, int i2c_addr, int reg, int val);
int tcpc_addr_write16(int port, int i2c_addr, int reg, int val);
int tcpc_addr_read(int port, int i2c_addr, int reg, int *val);
int tcpc_addr_read16(int port, int i2c_addr, int reg, int *val);
int tcpc_read_block(int port, int reg, uint8_t *in, int size);
int tcpc_write_block(int port, int reg, const uint8_t *out, int size);
int tcpc_xfer(int port, const uint8_t *out, int out_size,
		uint8_t *in, int in_size);
int tcpc_xfer_unlocked(int port, const uint8_t *out, int out_size,
		uint8_t *in, int in_size, int flags);

int tcpc_update8(int port, int reg,
		 uint8_t mask, enum mask_update_action action);
int tcpc_update16(int port, int reg,
		  uint16_t mask, enum mask_update_action action);

#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

static inline int tcpc_write(int port, int reg, int val)
{
	return tcpc_addr_write(port,
			       tcpc_config[port].i2c_info.addr_flags, reg, val);
}

static inline int tcpc_write16(int port, int reg, int val)
{
	return tcpc_addr_write16(port,
			tcpc_config[port].i2c_info.addr_flags, reg, val);
}

static inline int tcpc_read(int port, int reg, int *val)
{
	return tcpc_addr_read(port,
			      tcpc_config[port].i2c_info.addr_flags, reg, val);
}

static inline int tcpc_read16(int port, int reg, int *val)
{
	return tcpc_addr_read16(port,
			tcpc_config[port].i2c_info.addr_flags, reg, val);
}

static inline void tcpc_lock(int port, int lock)
{
	i2c_lock(tcpc_config[port].i2c_info.port, lock);
}

/* TCPM driver wrapper function */
static inline int tcpm_init(int port)
{
	int rv;

	rv = tcpc_config[port].drv->init(port);
	if (rv)
		return rv;

	/* Board specific post TCPC init */
	if (board_tcpc_post_init)
		rv = board_tcpc_post_init(port);

	return rv;
}

static inline int tcpm_release(int port)
{
	return tcpc_config[port].drv->release(port);
}

static inline int tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	return tcpc_config[port].drv->get_cc(port, cc1, cc2);
}

static inline bool tcpm_check_vbus_level(int port, enum vbus_level level)
{
	return tcpc_config[port].drv->check_vbus_level(port, level);
}

static inline int tcpm_select_rp_value(int port, int rp)
{
	return tcpc_config[port].drv->select_rp_value(port, rp);
}

static inline int tcpm_set_cc(int port, int pull)
{
	return tcpc_config[port].drv->set_cc(port, pull);
}

static inline int tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	return tcpc_config[port].drv->set_polarity(port, polarity);
}

static inline int tcpm_sop_prime_disable(int port)
{
#ifdef CONFIG_USB_PD_DECODE_SOP
	return tcpc_config[port].drv->sop_prime_disable(port);
#else
	return EC_SUCCESS;
#endif
}

static inline int tcpm_set_vconn(int port, int enable)
{
	return tcpc_config[port].drv->set_vconn(port, enable);
}

static inline int tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_config[port].drv->set_msg_header(port, power_role,
						     data_role);
}

static inline int tcpm_set_rx_enable(int port, int enable)
{
	return tcpc_config[port].drv->set_rx_enable(port, enable);
}

static inline void tcpm_enable_auto_discharge_disconnect(int port, int enable)
{
	const struct tcpm_drv *tcpc = tcpc_config[port].drv;

	if (tcpc->tcpc_enable_auto_discharge_disconnect)
		tcpc->tcpc_enable_auto_discharge_disconnect(port, enable);
}

/**
 * Reads a message using get_message_raw driver method and puts it into EC's
 * cache.
 */
int tcpm_enqueue_message(int port);

static inline int tcpm_transmit(int port, enum tcpm_transmit_type type,
				uint16_t header, const uint32_t *data)
{
	return tcpc_config[port].drv->transmit(port, type, header, data);
}

#ifdef CONFIG_USBC_PPC
static inline int tcpm_get_snk_ctrl(int port, bool *sinking)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if (tcpc_config[port].drv->get_snk_ctrl != NULL)
		rv = tcpc_config[port].drv->get_snk_ctrl(port, sinking);
	else
		*sinking = false;

	return rv;
}
static inline int tcpm_set_snk_ctrl(int port, int enable)
{
	if (tcpc_config[port].drv->set_snk_ctrl != NULL)
		return tcpc_config[port].drv->set_snk_ctrl(port, enable);
	else
		return EC_ERROR_UNIMPLEMENTED;
}

static inline int tcpm_get_src_ctrl(int port, bool *sourcing)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if (tcpc_config[port].drv->get_src_ctrl != NULL)
		rv = tcpc_config[port].drv->get_src_ctrl(port, sourcing);
	else
		*sourcing = false;

	return rv;
}
static inline int tcpm_set_src_ctrl(int port, int enable)
{
	if (tcpc_config[port].drv->set_src_ctrl != NULL)
		return tcpc_config[port].drv->set_src_ctrl(port, enable);
	else
		return EC_ERROR_UNIMPLEMENTED;
}
#endif

static inline void tcpc_alert(int port)
{
	tcpc_config[port].drv->tcpc_alert(port);
}

static inline void tcpc_discharge_vbus(int port, int enable)
{
	tcpc_config[port].drv->tcpc_discharge_vbus(port, enable);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
static inline int tcpm_auto_toggle_supported(int port)
{
	return !!tcpc_config[port].drv->drp_toggle;
}

static inline int tcpm_enable_drp_toggle(int port)
{
	return tcpc_config[port].drv->drp_toggle(port);
}
#else
static inline int tcpm_auto_toggle_supported(int port)
{
	return false;
}
#endif

static inline int tcpm_debug_accessory(int port, bool enable)
{
	if (tcpc_config[port].drv->debug_accessory)
		return tcpc_config[port].drv->debug_accessory(port, enable);
	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static inline int tcpm_enter_low_power_mode(int port)
{
	return tcpc_config[port].drv->enter_low_power_mode(port);
}
#endif

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
static inline int tcpc_i2c_read(const int port, const uint16_t addr_flags,
				const int reg, int *data)
{
	return tcpc_read(port, reg, data);
}

static inline int tcpc_i2c_write(const int port, const uint16_t addr_flags,
				 const int reg, int data)
{
	return tcpc_write(port, reg, data);
}
#endif

static inline int tcpm_get_chip_info(int port, int live,
				     struct ec_response_pd_chip_info_v1 *info)
{
	if (tcpc_config[port].drv->get_chip_info)
		return tcpc_config[port].drv->get_chip_info(port, live, info);
	return EC_ERROR_UNIMPLEMENTED;
}

#ifdef CONFIG_USB_PD_FRS_TCPC
static inline int tcpm_set_frs_enable(int port, int enable)
{
	const struct tcpm_drv *tcpc;
	int rv = EC_SUCCESS;

	/*
	 * set_frs_enable will be set to tcpci_tcp_fast_role_swap_enable
	 * if it is handled by the tcpci for the tcpc chipset
	 */
	tcpc = tcpc_config[port].drv;
	if (tcpc->set_frs_enable)
		rv = tcpc->set_frs_enable(port, enable);
	return rv;
}
#endif /* defined(CONFIG_USB_PD_FRS_TCPC) */

#else

/**
 * Initialize TCPM driver and wait for TCPC readiness.
 *
 * @param port Type-C port number
 *
 * @return EC_SUCCESS or error
 */
int tcpm_init(int port);

/**
 * Read the CC line status.
 *
 * @param port Type-C port number
 * @param cc1 pointer to CC status for CC1
 * @param cc2 pointer to CC status for CC2
 *
 * @return EC_SUCCESS or error
 */
int tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2);

/**
 * Check VBUS level
 *
 * @param port Type-C port number
 * @param level safe level voltage to check against
 *
 * @return False => VBUS not at level, True => VBUS at level
 */
bool tcpm_check_vbus_level(int port, enum vbus_level level);

/**
 * Set the value of the CC pull-up used when we are a source.
 *
 * @param port Type-C port number
 * @param rp One of enum tcpc_rp_value
 *
 * @return EC_SUCCESS or error
 */
int tcpm_select_rp_value(int port, int rp);

/**
 * Set the CC pull resistor. This sets our role as either source or sink.
 *
 * @param port Type-C port number
 * @param pull One of enum tcpc_cc_pull
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_cc(int port, int pull);

/**
 * Set polarity
 *
 * @param port Type-C port number
 * @param polarity port polarity
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity);

/**
 * Set Vconn.
 *
 * @param port Type-C port number
 * @param enable Enable/Disable Vconn
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_vconn(int port, int enable);

/**
 * Set PD message header to use for goodCRC
 *
 * @param port Type-C port number
 * @param power_role Power role to use in header
 * @param data_role Data role to use in header
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_msg_header(int port, int power_role, int data_role);

/**
 * Set RX enable flag
 *
 * @param port Type-C port number
 * @enable true for enable, false for disable
 *
 * @return EC_SUCCESS or error
 */
int tcpm_set_rx_enable(int port, int enable);

/**
 * Enable Auto Discharge Disconnect
 *
 * @param port Type-C port number
 * @param enable true for enable, false for disable
 */
void tcpm_enable_auto_discharge_disconnect(int port, int enable);

/**
 * Transmit PD message
 *
 * @param port Type-C port number
 * @param type Transmit type
 * @param header Packet header
 * @param cnt Number of bytes in payload
 * @param data Payload
 *
 * @return EC_SUCCESS or error
 */
int tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		  const uint32_t *data);

/**
 * TCPC is asserting alert
 *
 * @param port Type-C port number
 */
void tcpc_alert(int port);

#endif

/**
 * Gets the next waiting RX message.
 *
 * @param port Type-C port number
 * @param payload Pointer to location to copy payload of PD message
 * @param header The header of PD message
 *
 * @return EC_SUCCESS or error
 */
int tcpm_dequeue_message(int port, uint32_t *payload, int *header);

/**
 * Returns true if the tcpm has RX messages waiting to be consumed.
 */
int tcpm_has_pending_message(int port);

/**
 * Clear any pending messages in the RX queue.  This function must be
 * called from the same context as the caller of tcpm_dequeue_message to avoid
 * race conditions.
 */
void tcpm_clear_pending_messages(int port);

/**
 * Enable/Disable TCPC Fast Role Swap detection
 *
 * @param port Type-C port number
 * @param enable FRS enable (true) disable (false)
 * @return EC_SUCCESS on success, or an error
 */
int tcpm_set_frs_enable(int port, int enable);

#ifdef CONFIG_CMD_TCPC_DUMP
static inline void tcpm_dump_registers(int port)
{
	const struct tcpm_drv *tcpc = tcpc_config[port].drv;

	if (tcpc->dump_registers)
		tcpc->dump_registers(port);
	else
		tcpc_dump_std_registers(port);
}
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */

#endif
