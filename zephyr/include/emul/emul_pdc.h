/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_PDC_H_
#define ZEPHYR_INCLUDE_EMUL_PDC_H_

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>

#include <drivers/pdc.h>

/* Mirror PDC APIs to fetch or set emulated values */
typedef int (*emul_pdc_set_ucsi_version_t)(const struct emul *target,
					   uint16_t version);
typedef int (*emul_pdc_reset_t)(const struct emul *target);
typedef int (*emul_pdc_get_connector_reset_t)(const struct emul *dev,
					      enum connector_reset_t *type);
typedef int (*emul_pdc_set_capability_t)(const struct emul *target,
					 const struct capability_t *caps);
typedef int (*emul_pdc_set_connector_capability_t)(
	const struct emul *target, const union connector_capability_t *caps);
typedef int (*emul_pdc_get_ccom_t)(const struct emul *target, enum ccom_t *ccom,
				   enum drp_mode_t *dm);
typedef int (*emul_pdc_get_uor_t)(const struct emul *target, union uor_t *uor);
typedef int (*emul_pdc_get_pdr_t)(const struct emul *target, union pdr_t *pdr);
typedef int (*emul_pdc_get_sink_path_t)(const struct emul *target, bool *en);
typedef int (*emul_pdc_set_connector_status_t)(
	const struct emul *target,
	const struct connector_status_t *connector_status);
typedef int (*emul_pdc_set_error_status_t)(const struct emul *target,
					   const union error_status_t *es);

typedef int (*emul_pdc_set_vbus_t)(const struct emul *target,
				   const uint16_t *vbus);
typedef int (*emul_pdc_set_pdos_t)(const struct emul *target,
				   enum pdo_type_t pdo_type,
				   enum pdo_offset_t pdo_offset,
				   uint8_t num_pdos, bool port_partner_pdo,
				   uint32_t *pdos);
typedef int (*emul_pdc_set_info_t)(const struct emul *target,
				   const struct pdc_info_t *info);
typedef int (*emul_pdc_set_current_pdo_t)(const struct emul *target,
					  uint32_t pdo);
typedef int (*emul_pdc_get_current_flash_bank_t)(const struct emul *target,
						 uint8_t *bank);
typedef int (*emul_pdc_get_retimer_fw_t)(const struct emul *target,
					 bool *enable);

typedef int (*emul_pdc_set_response_delay_t)(const struct emul *target,
					     uint32_t delay_ms);
typedef int (*emul_pdc_get_requested_power_level_t)(
	const struct emul *target, enum usb_typec_current_t *level);

typedef int (*emul_pdc_get_reconnect_req_t)(const struct emul *target,
					    uint8_t *expecting, uint8_t *val);

__subsystem struct emul_pdc_api_t {
	emul_pdc_set_response_delay_t set_response_delay;
	emul_pdc_set_ucsi_version_t set_ucsi_version;
	emul_pdc_reset_t reset;
	emul_pdc_get_connector_reset_t get_connector_reset;
	emul_pdc_set_capability_t set_capability;
	emul_pdc_set_connector_capability_t set_connector_capability;
	emul_pdc_get_ccom_t get_ccom;
	emul_pdc_get_uor_t get_uor;
	emul_pdc_get_pdr_t get_pdr;
	emul_pdc_get_sink_path_t get_sink_path;
	emul_pdc_set_connector_status_t set_connector_status;
	emul_pdc_set_error_status_t set_error_status;
	emul_pdc_set_vbus_t set_vbus_voltage;
	emul_pdc_set_current_pdo_t set_current_pdo;
	emul_pdc_set_pdos_t set_pdos;
	emul_pdc_set_info_t set_info;
	emul_pdc_get_current_flash_bank_t get_current_flash_bank;
	emul_pdc_get_retimer_fw_t get_retimer;
	emul_pdc_get_requested_power_level_t get_requested_power_level;
	emul_pdc_get_reconnect_req_t get_reconnect_req;
};

static inline int emul_pdc_set_ucsi_version(const struct emul *target,
					    uint16_t version)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_ucsi_version) {
		return api->set_ucsi_version(target, version);
	}
	return -ENOSYS;
}

static inline int emul_pdc_reset(const struct emul *target)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->reset) {
		return api->reset(target);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_connector_reset(const struct emul *target,
					       enum connector_reset_t *type)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_connector_reset) {
		return api->get_connector_reset(target, type);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_capability(const struct emul *target,
					  const struct capability_t *caps)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_capability) {
		return api->set_capability(target, caps);
	}
	return -ENOSYS;
}

static inline int
emul_pdc_set_connector_capability(const struct emul *target,
				  const union connector_capability_t *caps)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_connector_capability) {
		return api->set_connector_capability(target, caps);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_ccom(const struct emul *target,
				    enum ccom_t *ccom, enum drp_mode_t *dm)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_ccom) {
		return api->get_ccom(target, ccom, dm);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_uor(const struct emul *target, union uor_t *uor)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_uor) {
		return api->get_uor(target, uor);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_pdr(const struct emul *target, union pdr_t *pdr)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_pdr) {
		return api->get_pdr(target, pdr);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_sink_path(const struct emul *target, bool *en)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_sink_path) {
		return api->get_sink_path(target, en);
	}
	return -ENOSYS;
}

static inline int
emul_pdc_set_connector_status(const struct emul *target,
			      const struct connector_status_t *connector_status)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_connector_status) {
		return api->set_connector_status(target, connector_status);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_error_status(const struct emul *target,
					    const union error_status_t *es)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_error_status) {
		return api->set_error_status(target, es);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_vbus(const struct emul *target,
				    const uint16_t *vbus)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_vbus_voltage) {
		return api->set_vbus_voltage(target, vbus);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_pdos(const struct emul *target,
				    enum pdo_type_t pdo_type,
				    enum pdo_offset_t pdo_offset,
				    uint8_t num_pdos, bool port_partner_pdo,
				    uint32_t *pdos)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_pdos) {
		return api->set_pdos(target, pdo_type, pdo_offset, num_pdos,
				     port_partner_pdo, pdos);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_info(const struct emul *target,
				    const struct pdc_info_t *info)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_info) {
		return api->set_info(target, info);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_current_pdo(const struct emul *target,
					   uint32_t pdo)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_current_pdo) {
		return api->set_current_pdo(target, pdo);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_current_flash_bank(const struct emul *target,
						  uint8_t *bank)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_current_flash_bank) {
		return api->get_current_flash_bank(target, bank);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_retimer_fw(const struct emul *target,
					  bool *enable)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_retimer) {
		return api->get_retimer(target, enable);
	}
	return -ENOSYS;
}

static inline int emul_pdc_set_response_delay(const struct emul *target,
					      uint32_t delay_ms)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->set_response_delay) {
		return api->set_response_delay(target, delay_ms);
	}
	return -ENOSYS;
}

static inline int
emul_pdc_get_requested_power_level(const struct emul *target,
				   enum usb_typec_current_t *level)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_requested_power_level) {
		return api->get_requested_power_level(target, level);
	}
	return -ENOSYS;
}

static inline int emul_pdc_get_reconnect_req(const struct emul *target,
					     uint8_t *expecting, uint8_t *val)
{
	if (!target || !target->backend_api) {
		return -ENOTSUP;
	}

	const struct emul_pdc_api_t *api = target->backend_api;

	if (api->get_reconnect_req) {
		return api->get_reconnect_req(target, expecting, val);
	}
	return -ENOSYS;
}

#endif /* ZEPHYR_INCLUDE_EMUL_PDC_H_ */
