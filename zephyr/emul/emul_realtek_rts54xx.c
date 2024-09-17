/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_pdc.h"
#include "emul/emul_smbus_ara.h"
#include "emul_realtek_rts54xx.h"
#include "usbc/utils.h"
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT realtek_rts54_pdc

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(realtek_rts5453_emul);

/* TODO(b/349609367): Do not rely on this test-only driver function. */
bool pdc_rts54xx_test_idle_wait(void);

static bool send_response(struct rts5453p_emul_pdc_data *data);

struct rts5453p_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Data required to simulate PD Controller */
	struct rts5453p_emul_pdc_data pdc_data;

	uint8_t port;

	/* Pointer to ara implementation so we can queue alert addresses ahead
	 * of the interrupt firing. */
	const struct emul *ara_emul;
};

struct rts5453p_emul_pdc_data *
rts5453p_emul_get_pdc_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->pdc_data;
}

static void set_ping_status(struct rts5453p_emul_pdc_data *data,
			    enum cmd_sts_t status, uint8_t length)
{
	LOG_DBG("ping status=0x%x, length=%d", status, length);
	data->read_ping = true;
	data->ping_status.cmd_sts = status;
	data->ping_status.data_len = length;
}

typedef int (*handler)(struct rts5453p_emul_pdc_data *data,
		       const union rts54_request *req);

static int unsupported(struct rts5453p_emul_pdc_data *data,
		       const union rts54_request *req)
{
	LOG_ERR("cmd=0x%X, subcmd=0x%X is not supported",
		req->req_subcmd.command_code, req->req_subcmd.sub_cmd);

	set_ping_status(data, CMD_ERROR, 0);
	return -EIO;
}

static int vendor_cmd_enable(struct rts5453p_emul_pdc_data *data,
			     const union rts54_request *req)
{
	data->vnd_command.raw = req->vendor_cmd_enable.sub_cmd3.raw;

	LOG_INF("VENDOR_CMD_ENABLE SMBUS=%d, FLASH=%d", data->vnd_command.smbus,
		data->vnd_command.flash);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int set_notification_enable(struct rts5453p_emul_pdc_data *data,
				   const union rts54_request *req)
{
	uint8_t port = req->set_notification_enable.port_num;

	data->notification_data[port] = req->set_notification_enable.data;
	LOG_INF("SET_NOTIFICATION_ENABLE port=%d, data=0x%X", port,
		data->notification_data[port]);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_ic_status(struct rts5453p_emul_pdc_data *data,
			 const union rts54_request *req)
{
	LOG_INF("GET_IC_STATUS");

	data->response.ic_status.byte_count = MIN(
		sizeof(struct rts54_ic_status) - 1, req->get_ic_status.sts_len);
	data->response.ic_status.fw_main_version = data->info.fw_version >> 16 &
						   BIT_MASK(8);
	data->response.ic_status.fw_sub_version[0] =
		data->info.fw_version >> 8 & BIT_MASK(8);
	data->response.ic_status.fw_sub_version[1] = data->info.fw_version &
						     BIT_MASK(8);

	data->response.ic_status.pd_revision[0] = data->info.pd_revision >> 8 &
						  BIT_MASK(8);
	data->response.ic_status.pd_revision[1] = data->info.pd_revision &
						  BIT_MASK(8);
	data->response.ic_status.pd_version[0] = data->info.pd_version >> 8 &
						 BIT_MASK(8);
	data->response.ic_status.pd_version[1] = data->info.pd_version &
						 BIT_MASK(8);

	data->response.ic_status.vid[1] = data->info.vid_pid >> 24 &
					  BIT_MASK(8);
	data->response.ic_status.vid[0] = data->info.vid_pid >> 16 &
					  BIT_MASK(8);
	data->response.ic_status.pid[1] = data->info.vid_pid >> 8 & BIT_MASK(8);
	data->response.ic_status.pid[0] = data->info.vid_pid & BIT_MASK(8);

	data->response.ic_status.is_flash_code =
		data->info.is_running_flash_code;
	data->response.ic_status.running_flash_bank_offset =
		data->info.running_in_flash_bank;

	memcpy(data->response.ic_status.project_name, data->info.project_name,
	       sizeof(data->response.ic_status.project_name));

	send_response(data);

	return 0;
}

static int get_lpm_ppm_info(struct rts5453p_emul_pdc_data *data,
			    const union rts54_request *req)
{
	LOG_INF("UCSI_GET_LPM_PPM_INFO");

	data->response.lpm_ppm_info.byte_count = sizeof(struct lpm_ppm_info_t);

	data->response.lpm_ppm_info.info = data->lpm_ppm_info;

	send_response(data);

	return 0;
}

static int block_read(struct rts5453p_emul_pdc_data *data,
		      const union rts54_request *req)
{
	data->read_ping = false;
	return 0;
}

static int ppm_reset(struct rts5453p_emul_pdc_data *data,
		     const union rts54_request *req)
{
	LOG_INF("PPM_RESET port=%d", req->ppm_reset.port_num);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int ack_cc_ci(struct rts5453p_emul_pdc_data *data,
		     const union rts54_request *req)
{
	uint16_t ci_mask;

	/*
	 * The bits which are set in the change indicator bits should clear any
	 * change indicator bits which are set the connector status message.
	 */
	ci_mask = ~req->ack_cc_ci.ci.raw_value;
	data->connector_status.raw_conn_status_change_bits &= ci_mask;

	LOG_INF("ACK_CC_CI port=%d, ci.raw = 0x%x", req->ack_cc_ci.port_num,
		req->ack_cc_ci.ci.raw_value);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int connector_reset(struct rts5453p_emul_pdc_data *data,
			   const union rts54_request *req)
{
	LOG_INF("CONNECTOR_RESET port=%d, hard_reset=%d",
		req->connector_reset.reset.connector_number,
		req->connector_reset.reset.reset_type);

	data->reset = req->connector_reset.reset;
	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_capability(struct rts5453p_emul_pdc_data *data,
			  const union rts54_request *req)
{
	LOG_INF("GET_CAPABILITY port=%d", req->get_capability.port_num);

	data->response.capability.byte_count = sizeof(struct capability_t);
	data->response.capability.caps = data->capability;
	send_response(data);

	return 0;
}

static int get_connector_capability(struct rts5453p_emul_pdc_data *data,
				    const union rts54_request *req)
{
	LOG_INF("GET_CONNECTOR_CAPABILITY port=%d",
		req->get_capability.port_num);

	data->response.capability.byte_count =
		sizeof(union connector_capability_t);
	data->response.connector_capability.caps = data->connector_capability;

	send_response(data);

	return 0;
}

static int tcpm_reset(struct rts5453p_emul_pdc_data *data,
		      const union rts54_request *req)
{
	LOG_INF("TCPM_RESET port=%d, reset_type=0x%X", req->tcpm_reset.port_num,
		req->tcpm_reset.reset_type);
	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_error_status(struct rts5453p_emul_pdc_data *data,
			    const union rts54_request *req)
{
	LOG_INF("GET_ERROR_STATUS port=%d", req->get_error_status.port_num);

	data->response.error_status.byte_count =
		sizeof(struct get_error_status_response) - 1;
	data->response.error_status.unrecognized_command =
		data->error.unrecognized_command;
	data->response.error_status.non_existent_connector_number =
		data->error.non_existent_connector_number;
	data->response.error_status.invalid_command_specific_param =
		data->error.invalid_command_specific_param;
	data->response.error_status.incompatible_connector_partner =
		data->error.incompatible_connector_partner;
	data->response.error_status.cc_communication_error =
		data->error.cc_communication_error;
	data->response.error_status.cmd_unsuccessful_dead_batt =
		data->error.cmd_unsuccessful_dead_batt;
	data->response.error_status.contract_negotiation_failed =
		data->error.contract_negotiation_failed;

	send_response(data);

	return 0;
}

static int get_connector_status(struct rts5453p_emul_pdc_data *data,
				const union rts54_request *req)
{
	LOG_INF("GET_CONNECTOR_STATUS port=%d",
		req->get_connector_status.port_num);

	data->response.connector_status.byte_count =
		sizeof(union connector_status_t);
	data->response.connector_status.status = data->connector_status;

	send_response(data);

	return 0;
}

static int get_rtk_status(struct rts5453p_emul_pdc_data *data,
			  const union rts54_request *req)
{
	union conn_status_change_bits_t conn_status_change_bits;

	conn_status_change_bits.raw_value =
		data->connector_status.raw_conn_status_change_bits;

	LOG_INF("GET_RTK_STATUS port=%d offset=%d sts_len=%d",
		req->get_rtk_status.port_num, req->get_rtk_status.offset,
		req->get_rtk_status.sts_len);

	data->response.rtk_status.byte_count =
		MIN(sizeof(struct get_rtk_status_response) - 1,
		    req->get_rtk_status.sts_len);

	/* Massage PD status into RTS54 response */
	/* BYTE 1-4 */
	data->response.rtk_status.pd_status.external_supply_charge =
		conn_status_change_bits.external_supply_change;
	data->response.rtk_status.pd_status.power_operation_mode_change =
		conn_status_change_bits.pwr_operation_mode;
	data->response.rtk_status.pd_status.provider_capabilities_change =
		conn_status_change_bits.supported_provider_caps;
	data->response.rtk_status.pd_status.negotiated_power_level_change =
		conn_status_change_bits.negotiated_power_level;
	data->response.rtk_status.pd_status.pd_reset_complete =
		conn_status_change_bits.pd_reset_complete;
	data->response.rtk_status.pd_status.supported_cam_change =
		conn_status_change_bits.supported_cam;
	data->response.rtk_status.pd_status.battery_charging_status_change =
		conn_status_change_bits.battery_charging_status;
	data->response.rtk_status.pd_status.port_partner_changed =
		conn_status_change_bits.connector_partner;
	data->response.rtk_status.pd_status.power_direction_changed =
		conn_status_change_bits.pwr_direction;
	data->response.rtk_status.pd_status.connect_change =
		conn_status_change_bits.connect_change;
	data->response.rtk_status.pd_status.error =
		conn_status_change_bits.error;

	/* BYTE 5 */
	data->response.rtk_status.supply = 0;
	data->response.rtk_status.port_operation_mode =
		data->connector_status.power_operation_mode & BIT_MASK(3);
	data->response.rtk_status.power_direction =
		data->connector_status.power_direction & BIT_MASK(1);
	data->response.rtk_status.connect_status =
		data->connector_status.connect_status & BIT_MASK(1);

	/* BYTE 6 */
	data->response.rtk_status.port_partner_flags =
		data->connector_status.conn_partner_flags;
	/* BYTE 7-10 */
	data->response.rtk_status.request_data_object =
		data->connector_status.rdo;
	/* BYTE 11 */
	data->response.rtk_status.port_partner_type =
		data->connector_status.conn_partner_type & BIT_MASK(3);
	data->response.rtk_status.battery_charging_status =
		data->connector_status.battery_charging_cap_status &
		BIT_MASK(2);
	data->response.rtk_status.pd_sourcing_vconn = data->vconn_sourcing;

	/* BYTE 12 */
	data->response.rtk_status.plug_direction =
		data->connector_status.orientation & BIT_MASK(1);

	/* Byte 14 */
	/* If the partner type supports PD (alternate mode or USB4(),
	 * set the alternate mode status as if all configuration is complete.
	 */
	if (data->connector_status.connect_status &&
	    data->connector_status.conn_partner_flags &
		    CONNECTOR_PARTNER_PD_CAPABLE) {
		/* 6 = DP Configure Command Done */
		data->response.rtk_status.alt_mode_related_status = 0x6;
	} else {
		/* 0 = Discovery Identity not done, partner doesn't support PD
		 */
		data->response.rtk_status.alt_mode_related_status = 0x0;
	}

	/* BYTE 16-17 */
	data->response.rtk_status.average_current_low = 0;
	data->response.rtk_status.average_current_high = 0;

	uint32_t voltage = data->connector_status.voltage_reading *
			   data->connector_status.voltage_scale * 5 / 50;

	/* BYTE 18-19 */
	data->response.rtk_status.voltage_reading_low = voltage & 0xFF;
	data->response.rtk_status.voltage_reading_high = voltage >> 8;

	data->read_offset = req->get_rtk_status.offset;

	send_response(data);

	return 0;
}

static int set_uor(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	LOG_INF("SET_UOR port=%d: uor=%x", req->set_uor.uor.connector_number,
		req->set_uor.uor.raw_value);

	data->uor = req->set_uor.uor;

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int set_pdr(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	LOG_INF("SET_PDR port=%d, swap_to_src=%d, swap_to_snk=%d, accept_pr_swap=%d}",
		req->set_pdr.pdr.connector_number, req->set_pdr.pdr.swap_to_src,
		req->set_pdr.pdr.swap_to_snk, req->set_pdr.pdr.accept_pr_swap);

	data->pdr = req->set_pdr.pdr;

	if (data->connector_status.power_operation_mode == PD_OPERATION &&
	    data->connector_status.connect_status &&
	    data->set_ccom_mode.ccom == BIT(2)) {
		if (data->pdr.swap_to_snk) {
			data->connector_status.power_direction = 0;
		} else if (data->pdr.swap_to_src) {
			data->connector_status.power_direction = 1;
		}
	}

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int set_rdo(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	LOG_INF("SET_RDO port=%d, rdo=0x%X", req->set_rdo.port_num,
		req->set_rdo.rdo);

	/* The SET_RDO command triggers a Request Object to be sent
	 * to the port partner when the LPM is a sink.
	 * The command is only valid when acting as a sink.
	 */
	if (data->connector_status.power_direction == 1) {
		/* We are a provider. Generate an error. */
		set_ping_status(data, CMD_ERROR, 0);
		return -EIO;
	}

	data->pdo.rdo = req->set_rdo.rdo;

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_rdo(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	LOG_INF("GET_RDO port=%d", req->set_rdo.port_num);

	data->response.get_rdo.byte_count = sizeof(struct get_rdo_response) - 1;

	if (data->connector_status.power_direction == 1) {
		/* We are the provider, return the RDO set by the partner.
		 */
		data->response.get_rdo.rdo = data->pdo.partner_rdo;
	} else {
		/* We are the consumer, return the RDO set using SET_RDO */
		data->response.get_rdo.rdo = data->pdo.rdo;
	}

	send_response(data);

	return 0;
}

static int set_tpc_rp(struct rts5453p_emul_pdc_data *data,
		      const union rts54_request *req)
{
	LOG_INF("SET_TPC_RP port=%d, value=0x%X", req->set_tpc_rp.port_num,
		req->set_tpc_rp.tpc_rp.raw_value);

	data->tpc_rp = req->set_tpc_rp.tpc_rp;

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int set_tpc_csd_operation_mode(struct rts5453p_emul_pdc_data *data,
				      const union rts54_request *req)
{
	LOG_INF("SET_TPC_CSD_OPERATION_MODE port=%d",
		req->set_tpc_csd_operation_mode.port_num);

	data->csd_op_mode = req->set_tpc_csd_operation_mode.op_mode;

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int set_ccom(struct rts5453p_emul_pdc_data *data,
		    const union rts54_request *req)
{
	LOG_INF("SET_CCOM port=%d", req->set_ccom.port_and_ccom.port_num);

	data->set_ccom_mode = req->set_ccom.port_and_ccom;

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int force_set_power_switch(struct rts5453p_emul_pdc_data *data,
				  const union rts54_request *req)
{
	LOG_INF("FORCE_SET_POWER_SWITCH port=%d",
		req->force_set_power_switch.port_num);

	data->set_power_switch_data = req->force_set_power_switch.data;

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int set_tpc_reconnect(struct rts5453p_emul_pdc_data *data,
			     const union rts54_request *req)
{
	LOG_INF("SET_TPC_RECONNECT port=%d", req->set_tpc_reconnect.port_num);

	data->set_tpc_reconnect_param = req->set_tpc_reconnect.param0;

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int read_power_level(struct rts5453p_emul_pdc_data *data,
			    const union rts54_request *req)
{
	LOG_INF("READ_POWER_LEVEL port=%d", req->read_power_level.port_num);

	memset(&data->response, 0, sizeof(data->response));
	send_response(data);

	return 0;
}

static int set_pdo(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	enum pdo_type_t pdo_type = req->set_pdo.pdo_type ? SOURCE_PDO :
							   SINK_PDO;
	uint8_t pdo_count;

	if (req->set_pdo.spr_pdo_number > PDO_OFFSET_MAX) {
		/* TODO - can the emulator generate an error response? */
		LOG_ERR("SET_PDO: SPR PDO count %d greater than %d",
			req->set_pdo.spr_pdo_number, PDO_OFFSET_MAX);
		return -EINVAL;
	}

	pdo_count = req->set_pdo.spr_pdo_number;

	LOG_INF("SET_PDO source type=%d, count=%d", pdo_type, pdo_count);

	emul_pdc_pdo_set_direct(&data->pdo, pdo_type, 0, pdo_count, LPM_PDO,
				req->set_pdo.pdos);

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);

	return 0;
}

static int get_pdos(struct rts5453p_emul_pdc_data *data,
		    const union rts54_request *req)
{
	enum pdo_type_t pdo_type = req->get_pdos.ucsi.pdo_type;
	enum pdo_source_t pdo_source = req->get_pdos.ucsi.pdo_source;
	enum pdo_offset_t pdo_offset = req->get_pdos.ucsi.pdo_offset;
	uint8_t pdo_count = req->get_pdos.ucsi.number_of_pdos + 1;

	/* GET_PDOS stops at the end if there's a requested overflow. */
	pdo_count = MIN(PDO_OFFSET_MAX - pdo_offset, pdo_count);

	LOG_INF("GET_PDO source %d, type=%d, offset=%d, count=%d", pdo_source,
		pdo_type, pdo_offset, pdo_count);

	memset(&data->response, 0, sizeof(data->response));
	emul_pdc_pdo_get_direct(&data->pdo, pdo_type, pdo_offset, pdo_count,
				pdo_source, data->response.get_pdos.pdos);
	data->response.get_pdos.byte_count = sizeof(uint32_t) * pdo_count;

	send_response(data);
	return 0;
}

static int get_cable_property(struct rts5453p_emul_pdc_data *data,
			      const union rts54_request *req)
{
	const union cable_property_t *ucsi_property = &data->cable_property;

	LOG_INF("GET_CABLE_PROPERTY property=%x", ucsi_property->raw_value[0]);
	memset(&data->response, 0, sizeof(data->response));

	/*
	 * The RTK command only returns 5 bytes of cable property, but
	 * they map to the first 5 bytes of the 8 byte UCSI response.
	 */
	BUILD_ASSERT(sizeof(data->response.get_cable_property) == 1 + 5);
	data->response.get_cable_property.byte_count =
		sizeof(data->response.get_cable_property) - 1;
	memcpy(data->response.get_cable_property.raw_value,
	       ucsi_property->raw_value,
	       data->response.get_cable_property.byte_count);

	send_response(data);
	return 0;
}

static int get_vdo(struct rts5453p_emul_pdc_data *data,
		   const union rts54_request *req)
{
	LOG_INF("GET_VDO = %x", req->get_vdo.vdo_req.raw_value);
	memset(&data->response, 0, sizeof(data->response));

	if (req->get_vdo.vdo_req.num_vdos > PDC_DISC_IDENTITY_VDO_COUNT) {
		LOG_ERR("Too many VDOs requested in GET_VDO.");
		return -EINVAL;
	}

	for (uint8_t i = 0; i < req->get_vdo.vdo_req.num_vdos; i++) {
		data->response.get_vdo.vdo[i] = data->vdos[i];
	}

	data->response.get_vdo.byte_count =
		sizeof(uint32_t) * req->get_vdo.vdo_req.num_vdos;

	send_response(data);
	return 0;
}

static int get_pch_data_status(struct rts5453p_emul_pdc_data *data,
			       const union rts54_request *req)
{
	uint32_t pch_data_status_output = 0;

	memset(&data->response, 0, sizeof(data->response));

	/* Data transfer Length */
	data->response.get_pch_data_status.byte_count = 5;

	/* Data_Connection_Present */
	pch_data_status_output =
		data->connector_status.connect_status ? BIT(0) : 0;
	/* Connection Orientation */
	pch_data_status_output |= data->connector_status.orientation ? BIT(1) :
								       0;
	/* USB2_Connection */
	pch_data_status_output |=
		data->connector_status.conn_partner_flags & BIT(0) ? BIT(4) : 0;
	/* USB3.2_Connection */
	pch_data_status_output |=
		data->connector_status.conn_partner_flags & BIT(0) ? BIT(5) : 0;
	/* DP_Connection */
	pch_data_status_output |=
		data->connector_status.conn_partner_flags & BIT(1) ? BIT(8) : 0;
	/* USB4 */
	pch_data_status_output |=
		data->connector_status.conn_partner_flags & BIT(2) ? BIT(23) :
								     0;
	pch_data_status_output |=
		data->connector_status.conn_partner_flags & BIT(3) ? BIT(23) :
								     0;

	data->response.get_pch_data_status.pch_data_status[0] =
		pch_data_status_output & 0xFF;
	data->response.get_pch_data_status.pch_data_status[1] =
		(pch_data_status_output >> 8) & 0xFF;
	data->response.get_pch_data_status.pch_data_status[2] =
		(pch_data_status_output >> 16) & 0xFF;
	data->response.get_pch_data_status.pch_data_status[3] =
		(pch_data_status_output >> 24) & 0xFF;
	LOG_INF("GET_PCH_DATA_STATUS PORT_NUM:%d data_status:0x%x",
		req->get_pch_data_status.port_num, pch_data_status_output);

	send_response(data);
	return 0;
}

static int set_frs_function(struct rts5453p_emul_pdc_data *data,
			    const union rts54_request *req)
{
	LOG_INF("SET_FRS_FUNCTION port=%d, setting %d",
		req->set_frs_function.port_num, req->set_frs_function.enable);

	data->frs_configured = true;
	data->frs_enabled = req->set_frs_function.enable;

	memset(&data->response, 0, sizeof(union rts54_response));
	send_response(data);
	return 0;
}

static bool send_response(struct rts5453p_emul_pdc_data *data)
{
	if (data->delay_ms > 0) {
		/* Simulate work and defer completion status */
		set_ping_status(data, CMD_DEFERRED, 0);
		k_work_schedule(&data->delay_work, K_MSEC(data->delay_ms));
		return true;
	}

	set_ping_status(data, CMD_COMPLETE, data->response.byte_count);

	return false;
}

static void delayable_work_handler(struct k_work *w)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(w);
	struct rts5453p_emul_pdc_data *data =
		CONTAINER_OF(dwork, struct rts5453p_emul_pdc_data, delay_work);

	set_ping_status(data, CMD_COMPLETE, data->response.byte_count);
}

struct commands {
	uint8_t code;
	enum {
		HANDLER = 0,
		SUBCMD = 1,
	} type;
	union {
		struct {
			uint8_t num_cmds;
			const struct commands *sub_cmd;
		};
		handler fn;
	};
};

#define SUBCMD_DEF(subcmd) \
	.type = SUBCMD, .sub_cmd = subcmd, .num_cmds = ARRAY_SIZE(subcmd)
#define HANDLER_DEF(handler) .type = HANDLER, .fn = handler

/* Data Sheet:
 * Realtek Power Delivery Command Interface By Realtek Version 3.3.18
 */
const struct commands sub_cmd_x01[] = {
	{ .code = 0xDA, HANDLER_DEF(vendor_cmd_enable) },
};

const struct commands sub_cmd_x08[] = {
	{ .code = 0x00, HANDLER_DEF(tcpm_reset) },
	{ .code = 0x01, HANDLER_DEF(set_notification_enable) },
	{ .code = 0x03, HANDLER_DEF(set_pdo) },
	{ .code = 0x04, HANDLER_DEF(set_rdo) },
	{ .code = 0x44, HANDLER_DEF(unsupported) },
	{ .code = 0x05, HANDLER_DEF(set_tpc_rp) },
	{ .code = 0x19, HANDLER_DEF(unsupported) },
	{ .code = 0x1A, HANDLER_DEF(unsupported) },
	{ .code = 0x1D, HANDLER_DEF(set_tpc_csd_operation_mode) },
	{ .code = 0x1F, HANDLER_DEF(set_tpc_reconnect) },
	{ .code = 0x20, HANDLER_DEF(unsupported) },
	{ .code = 0x21, HANDLER_DEF(force_set_power_switch) },
	{ .code = 0x23, HANDLER_DEF(unsupported) },
	{ .code = 0x24, HANDLER_DEF(unsupported) },
	{ .code = 0x26, HANDLER_DEF(unsupported) },
	{ .code = 0x27, HANDLER_DEF(unsupported) },
	{ .code = 0x28, HANDLER_DEF(unsupported) },
	{ .code = 0x2B, HANDLER_DEF(unsupported) },
	{ .code = 0x83, HANDLER_DEF(unsupported) },
	{ .code = 0x84, HANDLER_DEF(get_rdo) },
	{ .code = 0x85, HANDLER_DEF(unsupported) },
	{ .code = 0x99, HANDLER_DEF(unsupported) },
	{ .code = 0x9A, HANDLER_DEF(get_vdo) },
	{ .code = 0x9D, HANDLER_DEF(unsupported) },
	{ .code = 0xA2, HANDLER_DEF(unsupported) },
	{ .code = 0xF0, HANDLER_DEF(unsupported) },
	{ .code = 0xA6, HANDLER_DEF(unsupported) },
	{ .code = 0xA7, HANDLER_DEF(unsupported) },
	{ .code = 0xA8, HANDLER_DEF(unsupported) },
	{ .code = 0xA9, HANDLER_DEF(unsupported) },
	{ .code = 0xAA, HANDLER_DEF(unsupported) },
	{ .code = 0xE0, HANDLER_DEF(get_pch_data_status) },
	{ .code = 0xE1, HANDLER_DEF(set_frs_function) },
};

const struct commands sub_cmd_x0E[] = {
	{ .code = 0x01, HANDLER_DEF(ppm_reset) },
	{ .code = 0x03, HANDLER_DEF(connector_reset) },
	{ .code = 0x06, HANDLER_DEF(get_capability) },
	{ .code = 0x07, HANDLER_DEF(get_connector_capability) },
	{ .code = 0x08, HANDLER_DEF(set_ccom) },
	{ .code = 0x09, HANDLER_DEF(set_uor) },
	{ .code = 0x0B, HANDLER_DEF(set_pdr) },
	{ .code = 0x0C, HANDLER_DEF(unsupported) },
	{ .code = 0x0D, HANDLER_DEF(unsupported) },
	{ .code = 0x0E, HANDLER_DEF(unsupported) },
	{ .code = 0x0F, HANDLER_DEF(unsupported) },
	{ .code = 0x10, HANDLER_DEF(get_pdos) },
	{ .code = 0x11, HANDLER_DEF(get_cable_property) },
	{ .code = 0x12, HANDLER_DEF(get_connector_status) },
	{ .code = 0x13, HANDLER_DEF(get_error_status) },
	{ .code = 0x1E, HANDLER_DEF(read_power_level) },
	{ .code = 0x22, HANDLER_DEF(get_lpm_ppm_info) },
};

const struct commands sub_cmd_x12[] = {
	{ .code = 0x01, HANDLER_DEF(unsupported) },
	{ .code = 0x02, HANDLER_DEF(unsupported) },
};

const struct commands sub_cmd_x20[] = {
	{ .code = 0x00, HANDLER_DEF(unsupported) },
};

const struct commands rts54_commands[] = {
	{ .code = 0x01, SUBCMD_DEF(sub_cmd_x01) },
	{ .code = 0x08, SUBCMD_DEF(sub_cmd_x08) },
	{ .code = 0x09, HANDLER_DEF(get_rtk_status) },
	{ .code = 0x0A, HANDLER_DEF(ack_cc_ci) },
	{ .code = 0x0E, SUBCMD_DEF(sub_cmd_x0E) },
	{ .code = 0x12, SUBCMD_DEF(sub_cmd_x12) },
	{ .code = 0x20, SUBCMD_DEF(sub_cmd_x20) },
	{ .code = 0x3A, HANDLER_DEF(get_ic_status) },
	{ .code = 0x80, HANDLER_DEF(block_read) },
};

const int num_rts54_commands = ARRAY_SIZE(rts54_commands);

int process_request(struct rts5453p_emul_pdc_data *data,
		    const union rts54_request *req, uint8_t code,
		    const struct commands *cmds, int num_cmds)
{
	int i;

	LOG_INF("process request code=0x%X", code);

	set_ping_status(data, CMD_BUSY, 0);

	for (i = 0; i < num_cmds; i++) {
		if (cmds[i].code == code) {
			if (cmds[i].type == HANDLER) {
				return cmds[i].fn(data, req);
			} else {
				return process_request(data, req,
						       req->req_subcmd.sub_cmd,
						       cmds[i].sub_cmd,
						       cmds[i].num_cmds);
			}
		}
	}

	return unsupported(data, req);
}

/**
 * @brief Handle I2C start write message.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_start_write(const struct emul *emul, int reg)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("start_write cmd=%d", reg);

	memset(&data->request, 0, sizeof(union rts54_request));

	data->request.raw_data[0] = reg;

	return 0;
}

/**
 * @brief Function called for each byte of write message which is saved in
 *        data->msg_buf
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int rts5453p_emul_write_byte(const struct emul *emul, int reg,
				    uint8_t val, int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("write_byte reg=%d, val=0x%X, bytes=%d", reg, val, bytes);
	data->request.raw_data[bytes] = val;

	return 0;
}

/**
 * @brief Function which finalize write messages.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param bytes Number of bytes received in data->msg_buf
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_finish_write(const struct emul *emul, int reg,
				      int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("finish_write reg=%d, bytes=%d", reg, bytes);

	return process_request(data, &data->request,
			       data->request.request.command_code,
			       rts54_commands, num_rts54_commands);
}

/**
 * @brief Function which handles read messages. It expects that data->cur_cmd
 *        is set to command number which should be handled. It guarantees that
 *        data->num_to_read is set to number of bytes in data->msg_buf on
 *        successful handling read request. On error, data->num_to_read is
 *        always set to 0.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg Command selected by last write message. If data->cur_cmd is
 *            different than NO_CMD, then reg should equal to
 *            data->cur_cmd
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_start_read(const struct emul *emul, int reg)
{
	LOG_DBG("start_read reg=0x%X", reg);
	return 0;
}

/**
 * @brief Function called for each byte of read message. Byte from data->msg_buf
 *        is copied to read message response.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 */
static int rts5453p_emul_read_byte(const struct emul *emul, int reg,
				   uint8_t *val, int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	if (data->read_ping) {
		LOG_DBG("READING ping_raw_value=0x%X", data->ping_raw_value);
		*val = data->ping_raw_value;
	} else {
		uint8_t v;
		int o;

		/*
		 * Response byte 0 is always .byte_count.
		 * Remaining bytes are read starting at read_offset.
		 * Note that the byte following .byte_count is
		 * considered to be at offset 0.
		 */
		if (bytes > 0) {
			o = bytes + data->read_offset;
		} else {
			o = bytes;
		}

		v = data->response.raw_data[o];
		LOG_DBG("read_byte reg=0x%X, bytes=%d, offset=%d, val=0x%X",
			reg, bytes, data->read_offset, v);
		*val = v;
	}

	return 0;
}

/**
 * @brief Function type that is used by I2C device emulator at the end of
 *        I2C read message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 * @param bytes Number of bytes responeded to the I2C read message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_finish_read(const struct emul *emul, int reg,
				     int bytes)
{
	struct rts5453p_emul_pdc_data *data = rts5453p_emul_get_pdc_data(emul);

	LOG_DBG("finish_read reg=0x%X, bytes=%d", reg, bytes);
	if (data->read_ping) {
		data->read_ping = false;
	} else {
		data->read_offset = 0;
	}

	return 0;
}
/**
 * @brief Get currently accessed register, which always equals to selected
 *        command.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int rts5453p_emul_access_reg(const struct emul *emul, int reg, int bytes,
				    bool read)
{
	return reg;
}

static int emul_realtek_rts54xx_reset(const struct emul *target)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	/* Reset PDOs. */
	emul_pdc_pdo_reset(&data->pdo);

	data->set_ccom_mode.ccom = BIT(2); /* Realtek DRP bit 2 */
	data->frs_configured = false;

	return 0;
}

/* Device instantiation */

/**
 * @brief Set up a new RTS5453P emulator
 *
 * This should be called for each RTS5453P device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int rts5453p_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct rts5453p_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;

	i2c_common_emul_init(&data->common);

	data->pdc_data.read_offset = 0;

	data->pdc_data.reset.raw_value = 0xFF;
	data->pdc_data.ic_status.fw_main_version = 0xAB;
	data->pdc_data.ic_status.pd_version[0] = 0xCD;
	data->pdc_data.ic_status.pd_revision[0] = 0xEF;
	data->pdc_data.ic_status.byte_count =
		sizeof(struct rts54_ic_status) - 1;

	data->pdc_data.capability.bcdBCVersion = 0x1234;
	data->pdc_data.capability.bcdPDVersion = 0xBEEF;
	data->pdc_data.capability.bcdUSBTypeCVersion = 0xCAFE;

	data->pdc_data.connector_capability.op_mode_usb3 = 1;

	data->pdc_data.set_tpc_reconnect_param = 0xAA;

	emul_realtek_rts54xx_reset(emul);

	k_work_init_delayable(&data->pdc_data.delay_work,
			      delayable_work_handler);

	return 0;
}

static int emul_realtek_rts54xx_set_response_delay(const struct emul *target,
						   uint32_t delay_ms)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	LOG_INF("set_response_delay delay_ms=%d", delay_ms);
	data->delay_ms = delay_ms;

	return 0;
}

static int
emul_realtek_rts54xx_get_connector_reset(const struct emul *target,
					 union connector_reset_t *reset)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*reset = data->reset;

	return 0;
}

static int emul_realtek_rts54xx_set_capability(const struct emul *target,
					       const struct capability_t *caps)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->capability = *caps;

	return 0;
}

static int emul_realtek_rts54xx_set_connector_capability(
	const struct emul *target, const union connector_capability_t *caps)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->connector_capability = *caps;

	return 0;
}

static int emul_realtek_rts54xx_set_error_status(const struct emul *target,
						 const union error_status_t *es)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->error = *es;

	return 0;
}

static int emul_realtek_rts54xx_set_connector_status(
	const struct emul *target,
	const union connector_status_t *connector_status)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	union conn_status_change_bits_t change_bits;

	data->connector_status = *connector_status;

	change_bits.raw_value = connector_status->raw_conn_status_change_bits;

	if (change_bits.supported_provider_caps) {
		/* Turn off the sink path */
		data->set_power_switch_data.vbsin_en_control = 0;
		data->set_power_switch_data.vbsin_en = 0;
	}

	return 0;
}

static int emul_realtek_rts54xx_get_uor(const struct emul *target,
					union uor_t *uor)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*uor = data->uor;

	return 0;
}

static int emul_realtek_rts54xx_get_pdr(const struct emul *target,
					union pdr_t *pdr)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*pdr = data->pdr;

	return 0;
}

static int emul_realtek_rts54xx_get_rdo(const struct emul *target,
					uint32_t *rdo)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	/* Always return the RDO configured using SET_RDO or
	 * pdc_power_mgmt_set_new_power_request().
	 */
	*rdo = data->pdo.rdo;

	return 0;
}

static int emul_realtek_rts54xx_set_partner_rdo(const struct emul *target,
						uint32_t rdo)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->pdo.partner_rdo = rdo;

	return 0;
}

static int
emul_realtek_rts54xx_get_requested_power_level(const struct emul *target,
					       enum usb_typec_current_t *level)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	switch (data->tpc_rp.tpc_rp) {
	case 1:
		*level = TC_CURRENT_USB_DEFAULT;
		break;
	case 2:
		*level = TC_CURRENT_1_5A;
		break;
	case 3:
		*level = TC_CURRENT_3_0A;
		break;
	default:
		LOG_ERR("Invalid tpc_rp value 0x%X", data->tpc_rp.tpc_rp);
		return -EINVAL;
	}

	return 0;
}

static int emul_realtek_rts54xx_get_drp_mode(const struct emul *target,
					     enum drp_mode_t *dm)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*dm = DRP_INVALID;
	switch (data->csd_op_mode.csd_mode) {
	case 1:
		switch (data->csd_op_mode.drp_mode) {
		case 0:
			*dm = DRP_NORMAL;
			break;
		case 1:
			*dm = DRP_TRY_SRC;
			break;
		case 2:
			*dm = DRP_TRY_SNK;
			break;
		default:
			LOG_ERR("Invalid drp 0x%X", data->csd_op_mode.drp_mode);
			return -EINVAL;
		}
		break;
	case 0:
	case 2:
	default:
		LOG_ERR("CSD_MODE != DRP (0x%X), DRP mode is invalid",
			data->csd_op_mode.csd_mode);
		return -EINVAL;
	}

	return 0;
}

static int emul_realtek_rts54xx_get_ccom(const struct emul *target,
					 enum ccom_t *ccom)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	switch (data->set_ccom_mode.ccom) {
	case BIT(0):
		*ccom = CCOM_RP;
		break;
	case BIT(1):
		*ccom = CCOM_RD;
		break;
	case BIT(2):
		*ccom = CCOM_DRP;
		break;
	default:
		LOG_ERR("Invalid ccom mode 0x%X", data->set_ccom_mode.ccom);
		return -EINVAL;
	}

	return 0;
}

static int emul_realtek_rts54xx_get_sink_path(const struct emul *target,
					      bool *en)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*en = data->set_power_switch_data.vbsin_en_control &&
	      data->set_power_switch_data.vbsin_en == 3;

	return 0;
}

static int emul_realtek_rts54xx_get_reconnect_req(const struct emul *target,
						  uint8_t *expected,
						  uint8_t *val)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	*expected = 0x01;
	*val = data->set_tpc_reconnect_param;

	return 0;
}

static int emul_realtek_rts54xx_pulse_irq(const struct emul *target)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	struct rts5453p_emul_data *emul_data = target->data;
	const struct i2c_common_emul_cfg *cfg = target->cfg;

	emul_smbus_ara_queue_address(emul_data->ara_emul, emul_data->port,
				     cfg->addr);
	gpio_emul_input_set(data->irq_gpios.port, data->irq_gpios.pin, 1);
	gpio_emul_input_set(data->irq_gpios.port, data->irq_gpios.pin, 0);

	return 0;
}

static int emul_realtek_rts54xx_set_info(const struct emul *target,
					 const struct pdc_info_t *info)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->info = *info;

	return 0;
}

static int
emul_realtek_rts54xx_set_lpm_ppm_info(const struct emul *target,
				      const struct lpm_ppm_info_t *info)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	data->lpm_ppm_info = *info;

	return 0;
}

static int emul_realtek_rts54xx_set_vdo(const struct emul *target,
					uint8_t num_vdos, uint32_t *vdos)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	if (num_vdos > PDC_DISC_IDENTITY_VDO_COUNT) {
		return -EINVAL;
	}

	for (uint8_t i = 0; i < num_vdos; i++) {
		data->vdos[i] = vdos[i];
	}

	return 0;
}

static int emul_realtek_rts54xx_get_frs(const struct emul *target,
					bool *enabled)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);

	if (!data->frs_configured) {
		return -EIO;
	}

	*enabled = data->frs_enabled;

	return 0;
}

static int emul_realtek_rts54xx_get_pdos(const struct emul *target,
					 enum pdo_type_t pdo_type,
					 enum pdo_offset_t pdo_offset,
					 uint8_t num_pdos,
					 enum pdo_source_t source,
					 uint32_t *pdos)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	return emul_pdc_pdo_get_direct(&data->pdo, pdo_type, pdo_offset,
				       num_pdos, source, pdos);
}

static int emul_realtek_rts54xx_set_pdos(const struct emul *target,
					 enum pdo_type_t pdo_type,
					 enum pdo_offset_t pdo_offset,
					 uint8_t num_pdos,
					 enum pdo_source_t source,
					 const uint32_t *pdos)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	return emul_pdc_pdo_set_direct(&data->pdo, pdo_type, pdo_offset,
				       num_pdos, source, pdos);
}

static int
emul_realtek_rts54xx_get_cable_property(const struct emul *target,
					union cable_property_t *property)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	*property = data->cable_property;
	return 0;
}

static int
emul_realtek_rts54xx_set_cable_property(const struct emul *target,
					const union cable_property_t property)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	data->cable_property = property;
	return 0;
}

static int emul_realtek_rts54xx_idle_wait(const struct emul *target)
{
	/* TODO(b/349609367): This should be handled entirely in the emulator,
	 * not in the driver, and it should be specific to the passed-in target.
	 */

	ARG_UNUSED(target);

	if (pdc_rts54xx_test_idle_wait())
		return 0;
	return -ETIMEDOUT;
}

static int emul_realtek_rts54xx_set_vconn_sourcing(const struct emul *target,
						   bool enabled)
{
	struct rts5453p_emul_pdc_data *data =
		rts5453p_emul_get_pdc_data(target);
	data->vconn_sourcing = enabled;
	return 0;
}

struct emul_pdc_api_t emul_realtek_rts54xx_api = {
	.reset = emul_realtek_rts54xx_reset,
	.set_response_delay = emul_realtek_rts54xx_set_response_delay,
	.get_connector_reset = emul_realtek_rts54xx_get_connector_reset,
	.set_capability = emul_realtek_rts54xx_set_capability,
	.set_connector_capability =
		emul_realtek_rts54xx_set_connector_capability,
	.set_error_status = emul_realtek_rts54xx_set_error_status,
	.set_connector_status = emul_realtek_rts54xx_set_connector_status,
	.get_uor = emul_realtek_rts54xx_get_uor,
	.get_pdr = emul_realtek_rts54xx_get_pdr,
	.get_rdo = emul_realtek_rts54xx_get_rdo,
	.set_partner_rdo = emul_realtek_rts54xx_set_partner_rdo,
	.get_requested_power_level =
		emul_realtek_rts54xx_get_requested_power_level,
	.get_ccom = emul_realtek_rts54xx_get_ccom,
	.get_drp_mode = emul_realtek_rts54xx_get_drp_mode,
	.get_sink_path = emul_realtek_rts54xx_get_sink_path,
	.get_reconnect_req = emul_realtek_rts54xx_get_reconnect_req,
	.pulse_irq = emul_realtek_rts54xx_pulse_irq,
	.set_info = emul_realtek_rts54xx_set_info,
	.set_lpm_ppm_info = emul_realtek_rts54xx_set_lpm_ppm_info,
	.set_pdos = emul_realtek_rts54xx_set_pdos,
	.get_pdos = emul_realtek_rts54xx_get_pdos,
	.get_cable_property = emul_realtek_rts54xx_get_cable_property,
	.set_cable_property = emul_realtek_rts54xx_set_cable_property,
	.set_vdo = emul_realtek_rts54xx_set_vdo,
	.get_frs = emul_realtek_rts54xx_get_frs,
	.idle_wait = emul_realtek_rts54xx_idle_wait,
	.set_vconn_sourcing = emul_realtek_rts54xx_set_vconn_sourcing,
};

#define RTS5453P_EMUL_DEFINE(n)                                             \
	static struct rts5453p_emul_data rts5453p_emul_data_##n = {	\
		.common = {						\
			.start_write = rts5453p_emul_start_write,	\
			.write_byte = rts5453p_emul_write_byte,		\
			.finish_write = rts5453p_emul_finish_write,\
			.start_read = rts5453p_emul_start_read,	\
			.read_byte = rts5453p_emul_read_byte,		\
			.finish_read = rts5453p_emul_finish_read,	\
			.access_reg = rts5453p_emul_access_reg,		\
		},							\
		.pdc_data = {						\
			.irq_gpios = GPIO_DT_SPEC_INST_GET(n, irq_gpios), \
		},							\
		.port = USBC_PORT_FROM_DRIVER_NODE(DT_DRV_INST(n), pdc),  \
		.ara_emul = EMUL_DT_GET(DT_NODELABEL(smbus_ara_emul)),       \
	};       \
	static const struct i2c_common_emul_cfg rts5453p_emul_cfg_##n = {   \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),             \
		.data = &rts5453p_emul_data_##n.common,                     \
		.addr = DT_INST_REG_ADDR(n),                                \
	};                                                                  \
	EMUL_DT_INST_DEFINE(n, rts5453p_emul_init, &rts5453p_emul_data_##n, \
			    &rts5453p_emul_cfg_##n, &i2c_common_emul_api,   \
			    &emul_realtek_rts54xx_api)

DT_INST_FOREACH_STATUS_OKAY(RTS5453P_EMUL_DEFINE)

struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->common;
}
