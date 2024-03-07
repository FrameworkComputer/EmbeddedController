/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief PDC API for USB-C Power Management.
 */
#ifndef __CROS_EC_PDC_POWER_MGMT_H
#define __CROS_EC_PDC_POWER_MGMT_H

#include "usb_pd.h"

#include <stdbool.h>
#include <stdint.h>

#include <drivers/pdc.h>

/**
 * @brief Get the state of the port partner connection
 *
 * @param port USB-C port number
 *
 * @retval True if port is in connected state
 */
bool pdc_power_mgmt_is_connected(int port);

/**
 * @brief Get the number of USB-C ports in the system
 *
 * @retval CONFIG_USB_PD_PORT_MAX_COUNT
 */
uint8_t pdc_power_mgmt_get_usb_pd_port_count(void);

/**
 * @brief Set the passed charge port as active.
 *
 * @param charge_port   Charge port to be enabled.
 *
 * @retval EC_SUCCESS if the charge port is accepted
 */
int pdc_power_mgmt_set_active_charge_port(int charge_port);

/**
 * @brief Get CC polarity of the port
 *
 * @param port USB-C port number
 *
 * @retval (POLARITY_CC1 or POLARITY_CC1_DTS) for non-flipped connection or
 * (POLARITY_CC2 or POLARITY_CC2_DTS)
 */
enum tcpc_cc_polarity pdc_power_mgmt_pd_get_polarity(int port);

/**
 * @brief Get current data role
 *
 * @param port USB-C port number
 *
 * @retval PD_ROLE_UFP for UFP, or PD_ROLE_DFP
 */
enum pd_data_role pdc_power_mgmt_pd_get_data_role(int port);

/**
 * @brief Request power swap to Source
 *
 * @param port USB-C port number
 *
 * @retval void
 */
void pdc_power_mgmt_request_swap_to_src(int port);

/**
 * @brief Request power swap to Sink
 *
 * @param port USB-C port number
 *
 * @retval void
 */
void pdc_power_mgmt_request_swap_to_snk(int port);

/**
 * @brief Request data swap to UFP
 *
 * @param port USB-C port number
 *
 * @retval void
 */
void pdc_power_mgmt_request_swap_to_ufp(int port);

/**
 * @brief Request data swap to DFP
 *
 * @param port USB-C port number
 *
 * @retval void
 */
void pdc_power_mgmt_request_swap_to_dfp(int port);

/**
 * @brief Signal power request to indicate a charger update that affects the
 * port.
 *
 * @param port USB-C port number
 *
 * @retval void
 */
void pdc_power_mgmt_set_new_power_request(int port);

/**
 * @brief Get current power role
 *
 * @param port USB-C port number
 *
 * @retval PD_ROLE_SINK for SINK, or PD_ROLE_SOURCE
 */
enum pd_power_role pdc_power_mgmt_get_power_role(int port);

/**
 * @brief Get the current PD state of USB-C port
 *
 * @param port USB-C port number
 *
 * @retval PD state
 */
uint8_t pdc_power_mgmt_get_task_state(int port);

/**
 * @brief Check if PD communication is enabled
 *
 * @param port USB-C port number
 *
 * @retval true if it's enabled or false otherwise
 */
int pdc_power_mgmt_comm_is_enabled(int port);

/**
 * @brief Get current VCONN state of USB-C port
 *
 * @param port USB-C port number
 *
 * @retval true if the PDC is sourcing VCONN, else false
 */
bool pdc_power_mgmt_get_vconn_state(int port);

/**
 * @brief Check if port partner is dual role power
 *
 * @param port USB-C port number
 *
 * @retval true if partner is dual role power else false
 */
bool pdc_power_mgmt_get_partner_dual_role_power(int port);

/**
 * @brief Get port partner data swap capable status
 *
 * @param port USB-C port number
 *
 * @retval true if data swap capable, else false
 */
bool pdc_power_mgmt_get_partner_data_swap_capable(int port);

/**
 * @brief Check if port partner is USB comms capable
 *
 * @param port USB-C port number
 *
 * @retval true if partner port is capable of communication over USB data lines,
 * else false
 */
bool pdc_power_mgmt_get_partner_usb_comm_capable(int port);

/**
 * @brief Check if port partner is unconstrained power
 *
 * @param port USB-C port number
 *
 * @retval true if partner is unconstrained power, else false
 */
bool pdc_power_mgmt_get_partner_unconstr_power(int port);

/**
 * @brief Get the current CC line states from PD task
 *
 * @param port USB-C port number
 *
 * @retval CC state
 */
enum pd_cc_states pdc_power_mgmt_get_task_cc_state(int port);

/**
 * @brief Check if port partner is PD capable
 *
 * @param port USB-C port number
 *
 * @retval true if port partner is known to be PD capable
 */
bool pdc_power_mgmt_pd_capable(int port);

/**
 * @brief Measures and returns VBUS
 *
 * @param port USB-C port number
 *
 * @retval VBUS voltage
 */
uint32_t pdc_power_mgmt_get_vbus_voltage(int port);

/**
 * @brief Resets the PDC
 *
 * @param port USB-C port number
 *
 * @retval 0 if successful, error code otherwise
 */
int pdc_power_mgmt_reset(int port);

/**
 * @brief Get the source caps list sent by the port partner
 *
 * @param port USB-C port number
 * @retval pointer to source caps list
 */
const uint32_t *const pdc_power_mgmt_get_src_caps(int port);

/**
 * @brief Get the number of source caps sent by the port partner
 *
 * @param port USB-C port number
 * @retval number of source caps
 */
uint8_t pdc_power_mgmt_get_src_cap_cnt(int port);

/**
 * @brief Set dual role state, from among enum pd_dual_role_states
 *
 * @param port USB-C port number
 * @param state New state of dual-role port, selected from enum
 * pd_dual_role_states
 */
void pdc_power_mgmt_set_dual_role(int port, enum pd_dual_role_states state);

/**
 * @brief Get the current PD state name of USB-C port
 *
 * @param port USB-C port number
 * @retval name of task state
 */
const char *pdc_power_mgmt_get_task_state_name(int port);

/**
 * @brief Request a power role swap
 *
 * @param port USB-C port number
 */
void pdc_power_mgmt_request_power_swap(int port);

/**
 * @brief Request a data role swap
 *
 * @param port USB-C port number
 */
void pdc_power_mgmt_request_data_swap(int port);

/*
 * @brief Query info from the PD chip (USB PID/VID, FW ver, etc)
 *
 * @param port USB-C port number
 * @param pdc_info Output struct for chip info
 *
 * @retval 0 if successful or error code
 */
int pdc_power_mgmt_get_info(int port, struct pdc_info_t *pdc_info);

/**
 * @brief Query bus info from PDC used to access the chip
 *
 * @param port USB-C port number
 * @param pdc_info Output struct for bus info
 *
 * @retval 0 if successful or error code
 */
int pdc_power_mgmt_get_bus_info(int port, struct pdc_bus_info_t *pdc_bus_info);

/**
 * @brief Get current PD Revision
 *
 * @param port USB-C port number
 * @param type SOP* type
 *
 * @retval PD_REV10 for PD Revision 1.0
 *         PD_REV20 for PD Revision 2.0
 *         PD_REV30 for PD Revision 3.0
 */
int pdc_power_mgmt_get_rev(int port, enum tcpci_msg_type type);

/**
 * @brief Returns the sink caps list
 *
 * @param port USB-C port number
 *
 * @retval List of sink capabilities
 */
const uint32_t *const pdc_power_mgmt_get_snk_caps(int port);

/**
 * @brief Returns the number of sink caps
 *
 * @param port USB-C port number
 *
 * @retval Number of sink capabilities
 */
uint8_t pdc_power_mgmt_get_snk_cap_cnt(int port);

/**
 * @brief Get identity discovery state for this type and port
 *
 * @param port USB-C port number
 *
 * @retval Current discovery state
 */
enum pd_discovery_state
pdc_power_mgmt_get_identity_discovery(int port, enum tcpci_msg_type type);

/**
 * @brief Get port partner VID
 *
 * @param port USB-C port number
 *
 * @retval VID if available, 0 otherwise
 */
uint16_t pdc_power_mgmt_get_identity_vid(int port);

/**
 * @brief Get port partner PID
 *
 * @param port USB-C port number
 *
 * @retval PID if available, 0 otherwise
 */
uint16_t pdc_power_mgmt_get_identity_pid(int port);

/**
 * @brief Get port partner prodcut type
 *
 * @param port USB-C port number
 *
 * @retval product type if available, 0 otherwise
 */
uint8_t pdc_power_mgmt_get_product_type(int port);

/**
 * @brief Triggers hard or data reset
 *
 * @param port USB-C port number
 * @param reset_type Hard or Data
 *
 * @retval 0 if successful or error code
 */
int pdc_power_mgmt_connector_reset(int port, enum connector_reset reset_type);

#endif /* __CROS_EC_PDC_POWER_MGMT_H */
