/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file drivers/pdc.h
 * @brief Public APIs for Power Delivery Controller Chip drivers.
 *
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_PDC_H_
#define ZEPHYR_INCLUDE_DRIVERS_PDC_H_

#include "ucsi_v3.h"
#include "usb_pd.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract the 16-bit VID or PID from the 32-bit container in
 * `struct pdc_info_t`
 */
#define PDC_VIDPID_GET_VID(vidpid) (((vidpid) >> 16) & 0xFFFF)
#define PDC_VIDPID_GET_PID(vidpid) ((vidpid) & 0xFFFF)

/**
 * Extract the major, minor, and patch elements from a 32-bit version in
 * `struct pdc_info_t`
 */
#define PDC_FWVER_GET_MAJOR(fwver) (((fwver) >> 16) & 0xFF)
#define PDC_FWVER_GET_MINOR(fwver) (((fwver) >> 8) & 0xFF)
#define PDC_FWVER_GET_PATCH(fwver) ((fwver) & 0xFF)

/**
 * @brief Power Delivery Controller Information
 */
struct pdc_info_t {
	/** Firmware version running on the PDC */
	uint32_t fw_version;
	/** Power Delivery Revision supported by the PDC */
	uint16_t pd_revision;
	/** Power Delivery Version supported byt the PDC */
	uint16_t pd_version;
	/** VID:PID of the PDC (optional) */
	uint32_t vid_pid;
	/** Set to 1 if running from flash code (optional) */
	uint8_t is_running_flash_code;
	/** Set to the currently used flash bank (optional) */
	uint8_t running_in_flash_bank;
	/** Extra information (optional) */
	uint16_t extra;
};

/**
 * The type of interface used to access the PDC
 */
enum pdc_bus_type {
	PDC_BUS_TYPE_NONE = 0,
	PDC_BUS_TYPE_I2C,
	PDC_BUS_TYPE_MAX,
};

/**
 * @brief Bus info for PDC chip. This gets exposed via host command to enable
 *        passthrough access to the PDC from AP during firmware updates.
 */
struct pdc_bus_info_t {
	enum pdc_bus_type bus_type;
	union {
		struct i2c_dt_spec i2c;
	};
};

/**
 * @typedef
 * @brief These are the API function types
 */
typedef int (*pdc_get_ucsi_version_t)(const struct device *dev,
				      uint16_t *version);
typedef int (*pdc_reset_t)(const struct device *dev);
typedef int (*pdc_connector_reset_t)(const struct device *dev,
				     union connector_reset_t reset);
typedef int (*pdc_get_capability_t)(const struct device *dev,
				    struct capability_t *caps);
typedef int (*pdc_get_connector_capability_t)(
	const struct device *dev, union connector_capability_t *caps);
typedef int (*pdc_set_ccom_t)(const struct device *dev, enum ccom_t ccom,
			      enum drp_mode_t dm);
typedef int (*pdc_set_uor_t)(const struct device *dev, union uor_t uor);
typedef int (*pdc_set_pdr_t)(const struct device *dev, union pdr_t pdr);
typedef int (*pdc_set_sink_path_t)(const struct device *dev, bool en);
typedef int (*pdc_get_connector_status_t)(
	const struct device *dev, union connector_status_t *connector_status);
typedef int (*pdc_get_error_status_t)(const struct device *dev,
				      union error_status_t *es);
typedef void (*pdc_cci_handler_cb_t)(union cci_event_t cci_event,
				     void *cb_data);
typedef int (*pdc_set_handler_cb_t)(const struct device *dev,
				    pdc_cci_handler_cb_t cci_cb, void *cb_data);
typedef int (*pdc_get_vbus_t)(const struct device *dev, uint16_t *vbus);
typedef int (*pdc_get_pdos_t)(const struct device *dev,
			      enum pdo_type_t pdo_type,
			      enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			      bool port_partner_pdo, uint32_t *pdos);
typedef int (*pdc_get_rdo_t)(const struct device *dev, uint32_t *rdo);
typedef int (*pdc_set_rdo_t)(const struct device *dev, uint32_t rdo);
typedef int (*pdc_get_info_t)(const struct device *dev,
			      struct pdc_info_t *info);
typedef int (*pdc_get_bus_info_t)(const struct device *dev,
				  struct pdc_bus_info_t *info);
typedef int (*pdc_get_current_pdo_t)(const struct device *dev, uint32_t *pdo);
typedef int (*pdc_read_power_level_t)(const struct device *dev);
typedef int (*pdc_set_power_level_t)(const struct device *dev,
				     enum usb_typec_current_t tcc);
typedef int (*pdc_reconnect_t)(const struct device *dev);
typedef int (*pdc_get_current_flash_bank_t)(const struct device *dev,
					    uint8_t *bank);
typedef int (*pdc_update_retimer_fw_t)(const struct device *dev, bool enable);
typedef bool (*pdc_is_init_done_t)(const struct device *dev);
typedef int (*pdc_get_cable_property_t)(const struct device *dev,
					union cable_property_t *cable_prop);
typedef int (*pdc_get_vdo_t)(const struct device *dev, union get_vdo_t req,
			     uint8_t *req_list, uint32_t *vdo);
typedef int (*pdc_get_identity_discovery_t)(const struct device *dev,
					    bool *disc_state);
typedef int (*pdc_set_comms_state_t)(const struct device *dev, bool active);

/**
 * @cond INTERNAL_HIDDEN
 *
 * These are for internal use only, so skip these in public documentation.
 */
__subsystem struct pdc_driver_api_t {
	pdc_is_init_done_t is_init_done;
	pdc_get_ucsi_version_t get_ucsi_version;
	pdc_reset_t reset;
	pdc_connector_reset_t connector_reset;
	pdc_get_capability_t get_capability;
	pdc_get_connector_capability_t get_connector_capability;
	pdc_set_ccom_t set_ccom;
	pdc_set_uor_t set_uor;
	pdc_set_pdr_t set_pdr;
	pdc_set_sink_path_t set_sink_path;
	pdc_get_connector_status_t get_connector_status;
	pdc_get_error_status_t get_error_status;
	pdc_set_handler_cb_t set_handler_cb;
	pdc_get_vbus_t get_vbus_voltage;
	pdc_get_current_pdo_t get_current_pdo;
	pdc_get_pdos_t get_pdos;
	pdc_get_rdo_t get_rdo;
	pdc_set_rdo_t set_rdo;
	pdc_read_power_level_t read_power_level;
	pdc_get_info_t get_info;
	pdc_get_bus_info_t get_bus_info;
	pdc_set_power_level_t set_power_level;
	pdc_reconnect_t reconnect;
	pdc_get_current_flash_bank_t get_current_flash_bank;
	pdc_update_retimer_fw_t update_retimer;
	pdc_get_cable_property_t get_cable_property;
	pdc_get_vdo_t get_vdo;
	pdc_get_identity_discovery_t get_identity_discovery;
	pdc_set_comms_state_t set_comms_state;
};
/**
 * @endcond
 */

/**
 * @brief Tests if the PDC driver init process is complete
 *
 * @param dev PDC device structure pointer
 *
 * @retval true if PDC driver init process is complete, else false
 */
static inline bool pdc_is_init_done(const struct device *dev)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->is_init_done != NULL, "IS_INIT_DONE is not optional");

	return api->is_init_done(dev);
}

/**
 * @brief Trigger the PDC to read the power level when in Source mode.
 * @note  CCI Events set
 *            busy:  If PDC is Busy
 *            error: If the connector is in Sink Mode or is disconnected
 *            command_completed: power level in source mode ready
 *
 * @param dev PDC device structure pointer
 *
 * @retval 0 on API call success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_read_power_level(const struct device *dev)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->read_power_level != NULL,
		 "READ_POWER_LEVEL is not optional");

	return api->read_power_level(dev);
}

/**
 * @brief Read the version of the UCSI supported by the PDC. The version is
 *        read and cached during PDC initialization, so this call is
 *        synchronous and the version information is returned immediately
 *        to caller.
 * @note  CCI Events set
 *            <none>
 *
 * @param dev PDC device structure pointer
 *
 * @retval PDC Version number in BCD as an uint16. Format is JJMN, where (JJ –
 *         major version number, M – minor version number, N – sub-minor
 *         version number)
 * @retval -EINVAL if caps is NULL
 */
static inline int pdc_get_ucsi_version(const struct device *dev,
				       uint16_t *version)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_ucsi_version != NULL,
		 "GET_UCSI_VERSION is not optional");

	return api->get_ucsi_version(dev, version);
}

/**
 * @brief Resets the PDC
 * @note  CCI Events set
 *            reset_completed: PDC has been reset
 *
 * @param dev PDC device structure pointer
 *
 * @retval 0 on API call success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_reset(const struct device *dev)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->reset != NULL, "RESET is not optional");

	return api->reset(dev);
}

/**
 * @brief Resets a PDC connector
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: connector was reset
 *
 * @param dev PDC device structure pointer
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_connector_reset(const struct device *dev,
				      union connector_reset_t reset)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->connector_reset != NULL,
		 "CONNECTOR_RESET is not optional");

	return api->connector_reset(dev, reset);
}

/**
 * @brief Set the Sink FET state while in the Attached Sink State
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: sink path was set
 *
 * @param dev PDC device structure pointer
 * @param en true to enable the Sink FET or false to disable it
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_set_sink_path(const struct device *dev, bool en)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->set_sink_path != NULL, "SET_SINK_PATH is not optional");

	return api->set_sink_path(dev, en);
}

/**
 * @brief Gets the PDC device capabilities.
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: capability was retrieved
 *
 * @param dev PDC device structure pointer
 * @param caps pointer where the device capabilities are stored.
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if caps is NULL
 */
static inline int pdc_get_capability(const struct device *dev,
				     struct capability_t *caps)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_capability != NULL, "GET_CAPABILITY is not optional");

	return api->get_capability(dev, caps);
}

/**
 * @brief Gets the PDC connector status.
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: connector status was retrieved
 * @note cci_event.command_completed_indicator is set when the UCSI command
 *       completes.
 * @note cci_event.error_indicator is set if the UCSI command was
 *       unsuccessful.
 *
 * @param dev PDC device structure pointer
 * @param cs pointer where the connector status is stored.
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if cs is NULL
 */
static inline int
pdc_get_connector_status(const struct device *dev,
			 union connector_status_t *connector_status)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_connector_status != NULL,
		 "GET_CONNECTOR_STATUS is not optional");

	return api->get_connector_status(dev, connector_status);
}

/**
 * @brief Gets the details about an error, if the cci_event.error_indicator is
 *        set.
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: error status was retrieved
 *
 * @param dev PDC device structure pointer
 * @param es pointer where the error status is stored.
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if es is NULL
 */
static inline int pdc_get_error_status(const struct device *dev,
				       union error_status_t *es)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_error_status != NULL,
		 "GET_ERROR_STATUS is not optional");

	return api->get_error_status(dev, es);
}

/**
 * @brief Gets capabilities of a connector
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: connector caps were retrieved
 *
 * @param dev PDC device structure pointer
 * @param caps pointer where the connector capabilities are stored.
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if caps is NULL
 */
static inline int
pdc_get_connector_capability(const struct device *dev,
			     union connector_capability_t *caps)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_connector_capability != NULL,
		 "GET_CONNECTOR_CAPABILITY is not optional");

	return api->get_connector_capability(dev, caps);
}

/**
 * @brief Sets the CC operation mode of the PDC
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: CCOM was set
 *
 * @param dev PDC device structure pointer
 * @param ccom CC operation mode
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_set_ccom(const struct device *dev, enum ccom_t ccom,
			       enum drp_mode_t dm)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->set_ccom == NULL) {
		return -ENOSYS;
	}

	return api->set_ccom(dev, ccom, dm);
}

/**
 * @brief Sets the USB operation role of the PDC
 * @note CCI Events set
 *           busy: if the PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: UOR was set
 *
 * @param dev PDC device structure pointer
 * @param uor USB operation role
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_set_uor(const struct device *dev, union uor_t uor)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->set_uor != NULL, "SET_UOR is not optional");

	return api->set_uor(dev, uor);
}

/**
 * @brief Sets the Power direction role of the PDC
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: command was unsuccessful
 *           command_commpleted: PDR was set
 *
 * @param dev PDC device structure pointer
 * @param pdr Power direction role
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_set_pdr(const struct device *dev, union pdr_t pdr)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->set_pdr != NULL, "SET_PDR is not optional");

	return api->set_pdr(dev, pdr);
}

/**
 * @brief Sets the callback the driver uses to communicate events to the TCPM
 * @note CCI Events set
 *           <none>
 *
 * @param dev PDC device structure pointer
 * @param cci_cb pointer to callback
 * @param cb_data point to data that's passed to the callback
 */
static inline void pdc_set_handler_cb(const struct device *dev,
				      pdc_cci_handler_cb_t cci_cb,
				      void *cb_data)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->set_handler_cb != NULL, "SET_HANDLER_CB is not optional");

	api->set_handler_cb(dev, cci_cb, cb_data);
}

/**
 * @brief Reads the VBUS voltage
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: if port isn't connected
 *           command_commpleted: VBUS voltage was read
 *
 * @param dev PDC device structure pointer
 * @param voltage pointer to where the voltage is stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if voltage pointer is NULL
 */
static inline int pdc_get_vbus_voltage(const struct device *dev,
				       uint16_t *voltage)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_vbus_voltage != NULL,
		 "GET_VBUS_VOLTAGE is not optional");

	return api->get_vbus_voltage(dev, voltage);
}

/**
 * @brief Gets the Sink or Source PDOs associated with the connector, or its
 *        capabilities.
 * @note CCI Events set
 *           busy: if the PDC is busy
 *           error: the Port is not PD connected
 *           command_commpleted: PDOs have been retrieved
 *
 * @param dev PDC device structure pointer
 * @param partner_pdo true if requesting the PDOs from the attached device
 * @param offset starting offset of the first PDO to be returned. Valid values
 *               are 0 to 7.
 * @param num number of PDOs to return starting from the PDO offset. NOTE: the
 *            number of PDOs returned is num + 1.
 * @param prole Source for source PDOs or Sink for sink PDOs.
 * @param sc request the Source or Sink Capabilities instead of the PDOs. This
 *           parameter is only valid when partner_pdo is false.
 * @param pdos pointer to where the PDOs or Capabilities are stored.
 * @param es pointer where the error status is stored.
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if pdos is NULL
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_get_pdos(const struct device *dev,
			       enum pdo_type_t pdo_type,
			       enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			       bool port_partner_pdo, uint32_t *pdos)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->get_pdos == NULL) {
		return -ENOSYS;
	}

	return api->get_pdos(dev, pdo_type, pdo_offset, num_pdos,
			     port_partner_pdo, pdos);
}

/**
 * @brief Get information about the PDC
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: if the the info could not be retrieved
 *           command_completed: if the info was received
 *
 * @param dev PDC device structure pointer
 * @param info pointer to where the PDC information is stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if fw_version pointers is NULL
 */
static inline int pdc_get_info(const struct device *dev,
			       struct pdc_info_t *info)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_info != NULL, "GET_INFO is not optional");

	return api->get_info(dev, info);
}

/**
 * @brief Get bus interface info about the PDC
 *
 * @param dev PDC device structure pointer
 * @param info Output struct for bus info
 *
 * @retval 0 on success
 * @retval -EINVAL if info pointer is NULL
 */
static inline int pdc_get_bus_info(const struct device *dev,
				   struct pdc_bus_info_t *info)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_bus_info != NULL, "GET_INFO is not optional");

	return api->get_bus_info(dev, info);
}

/**
 * @brief Get the Requested Data Object sent to the Source
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: if the port partner is a Sink
 *           command_commpleted: if the RDO was retrieved
 *
 * @param dev PDC device structure pointer
 * @param rdo pointer to where the RDO is stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if rdo pointer is NULL
 */
static inline int pdc_get_rdo(const struct device *dev, uint32_t *rdo)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_rdo != NULL, "GET_RDO is not optional");

	return api->get_rdo(dev, rdo);
}

/**
 * @brief Sends a Requested Data Object to the attached Source
 * @note CCI Events set
 *           busy: if the PDC is busy
 *           error: if the port partner is a Sink
 *           command_commpleted: RDO was sent to port partner
 *
 * @param dev PDC device structure pointer
 * @param rdo RDO  to send to the Source
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 */
static inline int pdc_set_rdo(const struct device *dev, uint32_t rdo)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	return api->set_rdo(dev, rdo);
}

/**
 * @brief Get the currently selected PDO that was requested from the Source
 * @note CCI Events set
 *           busy: if the PDC is busy
 *           error: if the port isn't PD connected, or port partner is a Sink
 *           command_commpleted: PDO has been retrieved
 *
 * @param dev PDC device structure pointer
 * @param pdo pointer to where the PDO is stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if pdo pointer is NULL
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->get_current_pdo == NULL) {
		return -ENOSYS;
	}

	return api->get_current_pdo(dev, pdo);
}

/**
 * @brief Sets the TypeC Rp current resistor.
 * @note This command isn't compliant with the UCSI spec
 * @note CCI Events set
 *           busy: if the PDC is busy
 *           error: if the command couldn't be executed
 *           command_commpleted: the Rp was set
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if tcc is set to TC_CURRENT_PPM_DEFINED
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_set_power_level(const struct device *dev,
				      enum usb_typec_current_t tcc)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->set_power_level == NULL) {
		return -ENOSYS;
	}

	return api->set_power_level(dev, tcc);
}

/**
 * @brief Perform a Type-C reconnect
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: if port isn't connected
 *           command_commpleted: if the port has reconnected
 *
 * @param dev PDC device structure pointer
 *
 * @retval 0 on API call success
 * @retval -EIO on failure
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_reconnect(const struct device *dev)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->reconnect == NULL) {
		return -ENOSYS;
	}

	return api->reconnect(dev);
}

/**
 * @brief Get the current executing PDC flash bank
 * @note CCI Events set
 *           <none>
 *
 * @param dev PDC device structure pointer
 *
 * @retval 0 on API call success
 * @retval -ENOSYS if not implemented
 */
static inline int pdc_get_current_flash_bank(const struct device *dev,
					     uint8_t *bank)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* This is an optional feature, so it might not be implemented */
	if (api->get_current_flash_bank == NULL) {
		return -ENOSYS;
	}

	return api->get_current_flash_bank(dev, bank);
}

/**
 * @brief Command PD chip to enter retimer firmware update mode.
 *
 * @param enable True->enter, False->exit retimer firmware update mode.
 *
 * @retval 0 if success or I2C error.
 * @retval -EIO if input/output error.
 * @retval -ENOSYS if API not implemented.
 */
static inline int pdc_update_retimer_fw(const struct device *dev, bool enable)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	/* Temporarily optional feature, so it might not be implemented */
	if (api->update_retimer == NULL) {
		return -ENOSYS;
	}

	return api->update_retimer(dev, enable);
}

/**
 * @brief Gets the attached cable properties
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error: treated as non-emarker cable
 *           command_commpleted: capability was retrieved
 *
 * @param dev PDC device structure pointer
 * @param cable_prop pointer where the cable properties are stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if caps is NULL
 */
static inline int pdc_get_cable_property(const struct device *dev,
					 union cable_property_t *cable_prop)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_cable_property != NULL,
		 "GET_CABLE_PROPERTY is not optional");

	return api->get_cable_property(dev, cable_prop);
}

/**
 * @brief Get the Requested VDO objects
 * @note CCI Events set
 *           busy: if PDC is busy
 *           error:
 *           command_commpleted: if the VDOs were retrieved
 *
 * @param dev PDC device structure pointer
 * @param vdo pointer to where the VDOs are stored
 *
 * @retval 0 on success
 * @retval -EBUSY if not ready to execute the command
 * @retval -EINVAL if vdo pointer is NULL
 */
static inline int pdc_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
			      uint8_t *vdo_list, uint32_t *vdo)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->get_vdo != NULL, "GET_VDO is not optional");

	return api->get_vdo(dev, vdo_req, vdo_list, vdo);
}

/**
 * @brief get the state of the discovery process
 *
 * @param dev PDC device structure pointer
 * @param disc_state pointer where the discovery state is stored. True if
 * discovery is complete, else False
 *
 * @retval 0 on success
 * @retval -ENOSYS if not implemented
 * @retval -EINVAL if disc_state is NULL
 */
static inline int pdc_get_identity_discovery(const struct device *dev,
					     bool *disc_state)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	if (api->get_identity_discovery == NULL) {
		return -ENOSYS;
	}

	return api->get_identity_discovery(dev, disc_state);
}

/**
 * @brief Control if the driver can communicate with the PDC.
 *
 * @param dev PDC device structure pointer
 * @param comms_active True to allow PDC communication, false to end
 *        communication
 *
 * @retval 0 if success
 */
static inline int pdc_set_comms_state(const struct device *dev,
				      bool comms_active)
{
	const struct pdc_driver_api_t *api =
		(const struct pdc_driver_api_t *)dev->api;

	__ASSERT(api->set_comms_state != NULL,
		 "set_comms_state is not optional");

	return api->set_comms_state(dev, comms_active);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_PDC_H_ */
