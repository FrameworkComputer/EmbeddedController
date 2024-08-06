/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TI TPS6699X Power Delivery Controller Driver
 */

#include <assert.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
LOG_MODULE_REGISTER(tps6699x, CONFIG_USBC_LOG_LEVEL);
#include "tps6699x_cmd.h"
#include "tps6699x_reg.h"
#include "usbc/utils.h"

#include <drivers/pdc.h>
#include <timer.h>

#define DT_DRV_COMPAT ti_tps6699_pdc

/** @brief PDC IRQ EVENT bit */
#define PDC_IRQ_EVENT BIT(0)
/** @brief PDC COMMAND EVENT bit */
#define PDC_CMD_EVENT BIT(1)
/** @brief Requests the driver to enter the suspended state */
#define PDC_CMD_SUSPEND_REQUEST_EVENT BIT(2)

/**
 * @brief All raw_value data uses byte-0 for contains the register data was
 * written to, or read from, and byte-1 contains the length of said data. The
 * actual data starts at index 2
 */
#define RV_DATA_START 2

/**
 * @brief Number of TPS6699x ports detected
 */
#define NUM_PDC_TPS6699X_PORTS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* TODO: b/323371550 */
BUILD_ASSERT(NUM_PDC_TPS6699X_PORTS <= 2,
	     "tps6699x driver supports a maximum of 2 ports");

/**
 * @brief PDC commands
 */
enum cmd_t {
	/** No command */
	CMD_NONE,
	/** CMD_TRIGGER_PDC_RESET */
	CMD_TRIGGER_PDC_RESET,
	/** Set Notification Enable */
	CMD_SET_NOTIFICATION_ENABLE,
	/** PDC Reset */
	CMD_PPM_RESET,
	/** Connector Reset */
	CMD_CONNECTOR_RESET,
	/** Get Capability */
	CMD_GET_CAPABILITY,
	/** Get Connector Capability */
	CMD_GET_CONNECTOR_CAPABILITY,
	/** Set UOR */
	CMD_SET_UOR,
	/** Set PDR */
	CMD_SET_PDR,
	/** Get PDOs */
	CMD_GET_PDOS,
	/** Set PDOs */
	CMD_SET_PDOS,
	/** Get Connector Status */
	CMD_GET_CONNECTOR_STATUS,
	/** Get Error Status */
	CMD_GET_ERROR_STATUS,
	/** Get VBUS Voltage */
	CMD_GET_VBUS_VOLTAGE,
	/** Get IC Status */
	CMD_GET_IC_STATUS,
	/** Set CCOM */
	CMD_SET_CCOM,
	/** Read Power Level */
	CMD_READ_POWER_LEVEL,
	/** Get RDO */
	CMD_GET_RDO,
	/** Set Sink Path */
	CMD_SET_SINK_PATH,
	/** Get current Partner SRC PDO */
	CMD_GET_CURRENT_PARTNER_SRC_PDO,
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
	/** set Retimer into FW Update Mode */
	CMD_SET_RETIMER_FW_UPDATE_MODE,
	/** Get the cable properties */
	CMD_GET_CABLE_PROPERTY,
	/** Get VDO(s) of PDC, Cable, or Port partner */
	CMD_GET_VDO,
	/** CMD_GET_IDENTITY_DISCOVERY */
	CMD_GET_IDENTITY_DISCOVERY,
	/** CMD_GET_PCH_DATA_STATUS */
	CMD_GET_PCH_DATA_STATUS,
	/** CMD_SET_DRP_MODE */
	CMD_SET_DRP_MODE,
};

/**
 * @brief States of the main state machine
 */
enum state_t {
	/** Irq State */
	ST_IRQ,
	/** Init State */
	ST_INIT,
	/** Idle State */
	ST_IDLE,
	/** Error Recovery State */
	ST_ERROR_RECOVERY,
	/** TASK_WAIT */
	ST_TASK_WAIT,
	/** ST_SUSPENDED */
	ST_SUSPENDED,
};

/**
 * @brief PDC Config object
 */
struct pdc_config_t {
	/** I2C config */
	struct i2c_dt_spec i2c;
	/** pdc power path interrupt */
	struct gpio_dt_spec irq_gpios;
	/** connector number of this port */
	uint8_t connector_number;
	/** Notification enable bits */
	union notification_enable_t bits;
	/** Create thread function */
	void (*create_thread)(const struct device *dev);
};

/**
 * @brief PDC Data object
 */
struct pdc_data_t {
	/** State machine context */
	struct smf_ctx ctx;
	/** PDC device structure */
	const struct device *dev;
	/** Driver thread */
	k_tid_t thread;
	/** Driver thread's data */
	struct k_thread thread_data;
	/** GPIO interrupt callback */
	struct gpio_callback gpio_cb;
	/** Information about the PDC */
	struct pdc_info_t info;
	/** Init done flag */
	bool init_done;
	/** Callback data */
	void *cb_data;
	/** CCI Event */
	union cci_event_t cci_event;
	/** CC Event callback */
	struct pdc_callback *cc_cb;
	/** CC Event one-time callback. If it's NULL, cci_cb will be called. */
	struct pdc_callback *cc_cb_tmp;
	/** Asynchronous (CI) Event callbacks */
	sys_slist_t ci_cb_list;
	/** PDC status */
	union reg_status pdc_status;
	/** PDC interrupt */
	union reg_interrupt pdc_interrupt;
	/** PDC port control */
	union reg_port_control pdc_port_control;
	/** TypeC current */
	enum usb_typec_current_t tcc;
	/** Sink FET enable */
	bool snk_fet_en;
	/** Connector reset type */
	union connector_reset_t connector_reset;
	/** PDO Type */
	enum pdo_type_t pdo_type;
	/** PDO Offset */
	enum pdo_offset_t pdo_offset;
	/** Number of PDOS */
	uint8_t num_pdos;
	/** PDOS */
	uint32_t *pdos;
	/** Port Partner PDO */
	bool port_partner_pdo;
	/** CCOM */
	enum ccom_t ccom;
	/** PDR */
	union pdr_t pdr;
	/** UOR */
	union uor_t uor;
	/** DRP mode */
	enum drp_mode_t drp_mode;
	/** Pointer to user data */
	uint8_t *user_buf;
	/** Command mutex */
	struct k_mutex mtx;
	/** Vendor command to send */
	enum cmd_t cmd;
	/* VDO request list */
	enum vdo_type_t vdo_req_list[8];
	/* Request VDO */
	union get_vdo_t vdo_req;
	/* PDC event: Interrupt or Command */
	struct k_event pdc_event;
};

/**
 * @brief List of human readable state names for console debugging
 */
static const char *const state_names[] = {
	[ST_IRQ] = "IRQ",
	[ST_INIT] = "INIT",
	[ST_IDLE] = "IDLE",
	[ST_ERROR_RECOVERY] = "ERROR RECOVERY",
	[ST_TASK_WAIT] = "TASK_WAIT",
	[ST_SUSPENDED] = "SUSPENDED",
};

static const struct smf_state states[];

static void cmd_set_drp_mode(struct pdc_data_t *data);
static void cmd_set_tpc_rp(struct pdc_data_t *data);
static void cmd_get_rdo(struct pdc_data_t *data);
static void cmd_set_src_pdos(struct pdc_data_t *data);
static void cmd_set_snk_pdos(struct pdc_data_t *data);
static void cmd_get_ic_status(struct pdc_data_t *data);
static int cmd_get_ic_status_sync_internal(const struct i2c_dt_spec *i2c,
					   struct pdc_info_t *info);
static void cmd_get_vbus_voltage(struct pdc_data_t *data);
static void cmd_get_vdo(struct pdc_data_t *data);
static void cmd_get_identity_discovery(struct pdc_data_t *data);
static void cmd_get_pdc_data_status_reg(struct pdc_data_t *data);
static void task_gaid(struct pdc_data_t *data);
static void task_srdy(struct pdc_data_t *data);
static void task_ucsi(struct pdc_data_t *data,
		      enum ucsi_command_t ucsi_command);

/**
 * @brief PDC port data used in interrupt handler
 */
static struct pdc_data_t *pdc_data[NUM_PDC_TPS6699X_PORTS];

static enum state_t get_state(struct pdc_data_t *data)
{
	return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
	smf_set_state(SMF_CTX(data), &states[next_state]);
}

/**
 * Atomic flag to suspend sending new commands to chip
 *
 * This flag is shared across driver instances.
 *
 * TODO(b/323371550) When more than one PDC is supported, this flag will need
 * to be tracked per-chip.
 */
static atomic_t suspend_comms_flag = ATOMIC_INIT(0);

static void suspend_comms(void)
{
	atomic_set(&suspend_comms_flag, 1);
}

static void enable_comms(void)
{
	atomic_set(&suspend_comms_flag, 0);
}

static bool check_comms_suspended(void)
{
	return atomic_get(&suspend_comms_flag) != 0;
}

static void print_current_state(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;

	LOG_INF("DR%d: %s", cfg->connector_number,
		state_names[get_state(data)]);
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	const union cci_event_t cci = data->cci_event;

	LOG_INF("C%d: CCI=0x%x", cfg->connector_number, cci.raw_value);

	/*
	 * CC and CI events are separately reported. So, we need to call only
	 * one callback or the other.
	 */
	if (cci.connector_change) {
		pdc_fire_callbacks(&data->ci_cb_list, data->dev, cci);
	} else if (data->cc_cb_tmp) {
		data->cc_cb_tmp->handler(data->dev, data->cc_cb_tmp, cci);
		data->cc_cb_tmp = NULL;
	} else if (data->cc_cb) {
		data->cc_cb->handler(data->dev, data->cc_cb, cci);
	}

	data->cci_event.raw_value = 0;
}

static void st_irq_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_irq_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt pdc_interrupt;
	int rv;
	int i;
	bool interrupt_pending = false;

	/* Read the pending interrupt events */
	rv = tps_rd_interrupt_event(&cfg->i2c, &pdc_interrupt);
	if (rv) {
		LOG_ERR("Read interrupt events failed");
		goto error_recovery;
	}

	/* All raw_value data uses byte-0 for contains the register data was
	 * written too, or read from, and byte-1 contains the length of said
	 * data. The actual data starts at index 2. */
	LOG_DBG("IRQ PORT %d", cfg->connector_number);
	for (i = RV_DATA_START; i < sizeof(union reg_interrupt); i++) {
		LOG_DBG("Byte%d: %02x", i - RV_DATA_START,
			pdc_interrupt.raw_value[i]);
		if (pdc_interrupt.raw_value[i]) {
			interrupt_pending = true;
		}
	}
	LOG_DBG("\n");

	if (interrupt_pending) {
		/* Set CCI EVENT for connector change */
		data->cci_event.connector_change =
			(pdc_interrupt.plug_insert_or_removal |
			 pdc_interrupt.power_swap_complete |
			 pdc_interrupt.fr_swap_complete |
			 pdc_interrupt.data_swap_complete);
		/* Set CCI EVENT for not supported */
		data->cci_event.not_supported =
			pdc_interrupt.not_supported_received;
		/* Set CCI EVENT for vendor defined indicator (informs subsystem
		 * that an interrupt occurred */
		data->cci_event.vendor_defined_indicator = 1;

		/* TODO(b/345783692): Handle other interrupt bits. */

		/* Clear the pending interrupt events */
		rv = tps_rw_interrupt_clear(&cfg->i2c, &pdc_interrupt,
					    I2C_MSG_WRITE);
		if (rv) {
			LOG_ERR("Clear interrupt events failed");
			goto error_recovery;
		}

		/* Inform the subsystem of the event */
		call_cci_event_cb(data);
	}

	/* All done, transition back to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void st_init_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return;
	}

	/* Pre-fetch PDC chip info and save it in the driver struct */
	rv = cmd_get_ic_status_sync_internal(&cfg->i2c, &data->info);
	if (rv) {
		LOG_ERR("DR%d: Cannot obtain initial chip info (%d)",
			cfg->connector_number, rv);
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	LOG_INF("DR%d: FW Version %u.%u.%u", cfg->connector_number,
		PDC_FWVER_GET_MAJOR(data->info.fw_version),
		PDC_FWVER_GET_MINOR(data->info.fw_version),
		PDC_FWVER_GET_PATCH(data->info.fw_version));

	/* Set PDC notifications */
	data->cmd = CMD_SET_NOTIFICATION_ENABLE;

	/* Transition to the idle state */
	set_state(data, ST_IDLE);
	return;
}

static void st_init_exit(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Inform the driver that the init process is complete */
	/* TODO: Make sure this makes sense if the next state is suspend. It may
	 * be possible to remove ST_INIT entirely by doing this in the init
	 * function.
	 */
	data->init_done = true;
}

static void st_idle_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* Reset the command if no pending PDC_CMD_EVENT */
	if (!k_event_test(&data->pdc_event, PDC_CMD_EVENT)) {
		data->cmd = CMD_NONE;
	}
}

static void st_idle_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	uint32_t events;

	/* Wait for interrupt or a command to send */
	events = k_event_wait(&data->pdc_event,
			      (PDC_IRQ_EVENT | PDC_CMD_EVENT |
			       PDC_CMD_SUSPEND_REQUEST_EVENT),
			      false, K_FOREVER);

	if (check_comms_suspended()) {
		/* Do not start executing commands or processing IRQs if
		 * suspended. We don't need to check the event flag, it is
		 * only needed to wake this thread.
		 */
		set_state(data, ST_SUSPENDED);
		return;
	}

	if (events & PDC_IRQ_EVENT) {
		k_event_clear(&data->pdc_event, PDC_IRQ_EVENT);
		/* Handle interrupt */
		set_state(data, ST_IRQ);
		return;
	} else if (events & PDC_CMD_EVENT) {
		k_event_clear(&data->pdc_event, PDC_CMD_EVENT);
		/* Handle command */
		/* TODO(b/345783692): enum ucsi_command_t should be extended to
		 * contain vendor-defined commands. That way, switch statements
		 * like this can operate on that enum, and we won't need a bunch
		 * of driver code just to convert from generic commands to
		 * driver commands.
		 */
		switch (data->cmd) {
		case CMD_NONE:
			break;
		case CMD_TRIGGER_PDC_RESET:
			task_gaid(data);
			break;
		case CMD_SET_NOTIFICATION_ENABLE:
			task_ucsi(data, UCSI_SET_NOTIFICATION_ENABLE);
			break;
		case CMD_PPM_RESET:
			task_ucsi(data, UCSI_PPM_RESET);
			break;
		case CMD_CONNECTOR_RESET:
			task_ucsi(data, UCSI_CONNECTOR_RESET);
			break;
		case CMD_GET_CAPABILITY:
			task_ucsi(data, UCSI_GET_CAPABILITY);
			break;
		case CMD_GET_CONNECTOR_CAPABILITY:
			task_ucsi(data, UCSI_GET_CONNECTOR_CAPABILITY);
			break;
		case CMD_SET_UOR:
			task_ucsi(data, UCSI_SET_UOR);
			break;
		case CMD_SET_PDR:
			task_ucsi(data, UCSI_SET_PDR);
			break;
		case CMD_GET_PDOS:
			task_ucsi(data, UCSI_GET_PDOS);
			break;
		case CMD_SET_PDOS:
			if (data->pdo_type == SOURCE_PDO)
				cmd_set_src_pdos(data);
			else
				cmd_set_snk_pdos(data);
			break;
		case CMD_GET_CONNECTOR_STATUS:
			task_ucsi(data, UCSI_GET_CONNECTOR_STATUS);
			break;
		case CMD_GET_ERROR_STATUS:
			task_ucsi(data, UCSI_GET_ERROR_STATUS);
			break;
		case CMD_GET_VBUS_VOLTAGE:
			cmd_get_vbus_voltage(data);
			break;
		case CMD_GET_IC_STATUS:
			cmd_get_ic_status(data);
			break;
		case CMD_SET_CCOM:
			task_ucsi(data, UCSI_SET_CCOM);
			break;
		case CMD_READ_POWER_LEVEL:
			task_ucsi(data, UCSI_READ_POWER_LEVEL);
			break;
		case CMD_GET_RDO:
			cmd_get_rdo(data);
			break;
		case CMD_SET_SINK_PATH:
			task_srdy(data);
			break;
		case CMD_GET_CURRENT_PARTNER_SRC_PDO:
			task_ucsi(data, UCSI_GET_PDOS);
			break;
		case CMD_SET_TPC_RP:
			cmd_set_tpc_rp(data);
			break;
		case CMD_SET_DRP_MODE:
			cmd_set_drp_mode(data);
			break;
		case CMD_SET_RETIMER_FW_UPDATE_MODE:
			task_ucsi(data, UCSI_SET_RETIMER_MODE);
			break;
		case CMD_GET_CABLE_PROPERTY:
			task_ucsi(data, UCSI_GET_CABLE_PROPERTY);
			break;
		case CMD_GET_VDO:
			cmd_get_vdo(data);
			break;
		case CMD_GET_IDENTITY_DISCOVERY:
			cmd_get_identity_discovery(data);
			break;
		case CMD_GET_PCH_DATA_STATUS:
			cmd_get_pdc_data_status_reg(data);
		}
	}
}

static void st_idle_exit(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Clear the CCI EVENT */
	data->cci_event.raw_value = 0;
}

static void st_error_recovery_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_error_recovery_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Don't continue trying if we are suspending communication */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return;
	}

	/* TODO: Add proper error recovery */
	/* Currently this state is entered when an I2C command fails */

	/* Command has completed with an error */
	data->cci_event.command_completed = 1;
	data->cci_event.error = 1;

	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle */
	set_state(data, ST_IDLE);
	return;
}

static void st_suspended_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_suspended_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Stay here while suspended */
	if (check_comms_suspended()) {
		return;
	}

	set_state(data, ST_INIT);
}

static void cmd_set_drp_mode(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_configuration pdc_port_configuration;
	int rv;

	/* Read PDC port configuration */
	rv = tps_rw_port_configuration(&cfg->i2c, &pdc_port_configuration,
				       I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Read port configuration failed");
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	/* Modify */
	switch (data->drp_mode) {
	case DRP_NORMAL:
	case DRP_TRY_SRC:
		pdc_port_configuration.typec_support_options = data->drp_mode;
		break;
	default:
		LOG_ERR("Unsupported DRP mode");
		set_state(data, ST_IDLE);
		return;
	}

	/* Write PDC port configuration */
	rv = tps_rw_port_configuration(&cfg->i2c, &pdc_port_configuration,
				       I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Write port configuration failed");
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;
}

static void cmd_set_tpc_rp(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_control pdc_port_control;
	int rv;

	/* Read PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Read port control failed");
		goto error_recovery;
	}

	/* Modify */
	switch (data->tcc) {
	case TC_CURRENT_PPM_DEFINED:
		LOG_ERR("Unsupported type: TC_CURRENT_PPM_DEFINED");
		set_state(data, ST_IDLE);
		return;
	case TC_CURRENT_3_0A:
		pdc_port_control.typec_current = 2;
		break;
	case TC_CURRENT_1_5A:
		pdc_port_control.typec_current = 1;
		break;
	case TC_CURRENT_USB_DEFAULT:
		pdc_port_control.typec_current = 0;
		break;
	}

	/* Write PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Write port control failed");
		goto error_recovery;
	}

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_set_src_pdos(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_transmit_source_capabilities pdc_tx_src_capabilities;
	int rv;

	/* Support SPR only */
	if (data->num_pdos == 0 || data->num_pdos > 7)
		goto error_recovery;

	/* Read PDC Transmit Source Capabilities */
	rv = tps_rw_transmit_source_capabilities(
		&cfg->i2c, &pdc_tx_src_capabilities, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Read transmit source capabilities failed");
		goto error_recovery;
	}

	pdc_tx_src_capabilities.number_of_valid_pdos = data->num_pdos;
	memcpy(pdc_tx_src_capabilities.spr_tx_source_pdo, data->pdos,
	       sizeof(uint32_t) * data->num_pdos);

	/* Write PDC Transmit Source Capabilities */
	rv = tps_rw_transmit_source_capabilities(
		&cfg->i2c, &pdc_tx_src_capabilities, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Write transmit source capabilities failed");
		goto error_recovery;
	}

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_set_snk_pdos(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_transmit_sink_capabilities pdc_tx_snk_capabilities;
	int rv;

	/* Support SPR only */
	if (data->num_pdos == 0 || data->num_pdos > 7)
		goto error_recovery;

	/* Read PDC Transmit Sink Capabilities */
	rv = tps_rw_transmit_sink_capabilities(
		&cfg->i2c, &pdc_tx_snk_capabilities, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Read transmit sink capabilities failed");
		goto error_recovery;
	}

	pdc_tx_snk_capabilities.number_of_valid_pdos = data->num_pdos;
	memcpy(pdc_tx_snk_capabilities.spr_tx_sink_pdo, data->pdos,
	       sizeof(uint32_t) * data->num_pdos);

	/* Write PDC Transmit Sink Capabilities */
	rv = tps_rw_transmit_sink_capabilities(
		&cfg->i2c, &pdc_tx_snk_capabilities, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Write transmit sink capabilities failed");
		goto error_recovery;
	}

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_rdo(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_active_rdo_contract active_rdo_contract;
	uint32_t *rdo = (uint32_t *)data->user_buf;
	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("Null buffer; can't read RDO");
		goto error_recovery;
	}

	rv = tps_rd_active_rdo_contract(&cfg->i2c, &active_rdo_contract);
	if (rv) {
		LOG_ERR("Failed to read active RDO");
		goto error_recovery;
	}

	*rdo = active_rdo_contract.rdo;

	/* TODO(b/345783692): Put command-completed logic in common code. */
	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_vdo(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_received_identity_data_object received_identity_data_object;
	uint32_t *vdo = (uint32_t *)data->user_buf;
	int rv;

	if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP) {
		rv = tps_rd_received_sop_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("Failed to read partner identity ACK");
			goto error_recovery;
		}
	} else if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP_PRIME) {
		rv = tps_rd_received_sop_prime_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("Failed to read cable identity ACK");
			goto error_recovery;
		}
	} else {
		/* Unsupported */
		LOG_ERR("Unsupported VDO origin");
		goto error_recovery;
	}

	for (int i = 0; i < data->vdo_req.num_vdos; i++) {
		switch (data->vdo_req_list[i]) {
		case VDO_ID_HEADER:
			vdo[i] = received_identity_data_object.vdo[0];
			break;
		case VDO_CERT_STATE:
			vdo[i] = received_identity_data_object.vdo[1];
			break;
		case VDO_PRODUCT:
			vdo[i] = received_identity_data_object.vdo[2];
			break;
		default:
			/* Unsupported */
			vdo[i] = 0;
		}
	}

	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_identity_discovery(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_received_identity_data_object received_identity_data_object;
	bool *disc_state = (bool *)data->user_buf;
	int rv;

	if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP) {
		rv = tps_rd_received_sop_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("Failed to read partner VDO");
			goto error_recovery;
		}
	} else if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP_PRIME) {
		rv = tps_rd_received_sop_prime_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("Failed to read cable VDO");
			goto error_recovery;
		}
	} else {
		/* Unsupported */
		LOG_ERR("Unsupported VDO origin");
		goto error_recovery;
	}

	*disc_state = (received_identity_data_object.response_type == 1) ?
			      true :
			      false;

	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

/**
 * @brief Helper function for internal use that synchronously obtains FW ver
 *        and TX identity.
 *
 * @param i2c Pointer to the I2C bus DT spec
 * @param info Output param for chip info
 * @return 0 on success or an error code
 */
static int cmd_get_ic_status_sync_internal(const struct i2c_dt_spec *i2c,
					   struct pdc_info_t *info)
{
	union reg_version version;
	union reg_tx_identity tx_identity;
	int rv;
	union reg_customer_use customer_val;

	if (info == NULL) {
		return -EINVAL;
	}

	rv = tps_rd_version(i2c, &version);
	if (rv) {
		LOG_ERR("Failed to read version");
		return rv;
	}

	rv = tps_rw_customer_use(i2c, &customer_val, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Failed to read customer register");
		return rv;
	}

	rv = tps_rw_tx_identity(i2c, &tx_identity, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Failed to read Tx identity");
		return rv;
	}

	/* TI Is running flash code */
	info->is_running_flash_code = 1;

	/* TI FW main version */
	info->fw_version = version.version;

	/* FW config version for this FW version */
	info->fw_config_version = customer_val.fw_config_version;

	/* TI VID PID (little-endian) */
	info->vid_pid = (*(uint16_t *)tx_identity.vendor_id) << 16 |
			*(uint16_t *)tx_identity.product_id;

	/* TI Running flash bank offset */
	info->running_in_flash_bank = 0;

	/* TI PD Revision (big-endian) */
	info->pd_revision = 0x0000;

	/* TI PD Version (big-endian) */
	info->pd_version = 0x0000;

	return 0;
}

static void cmd_get_ic_status(struct pdc_data_t *data)
{
	struct pdc_info_t *info = (struct pdc_info_t *)data->user_buf;
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = cmd_get_ic_status_sync_internal(&cfg->i2c, info);
	if (rv) {
		LOG_ERR("Could not get chip info (%d)", rv);
		goto error_recovery;
	}

	/* Retain a cached copy of this data */
	data->info = *info;

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_vbus_voltage(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_adc_results adc_results;

	uint16_t *vbus = (uint16_t *)data->user_buf;
	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("Null user buffer; can't read VBUS voltage");
		goto error_recovery;
	}

	rv = tps_rd_adc_results(&cfg->i2c, &adc_results);
	if (rv) {
		LOG_ERR("Failed to read ADC results");
		goto error_recovery;
	}

	*vbus = cfg->connector_number ? adc_results.pa_vbus :
					adc_results.pb_vbus;

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_pdc_data_status_reg(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data_status data_status;

	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("Null user buffer; can't read data status reg");
		goto error_recovery;
	}

	rv = tps_rd_data_status_reg(&cfg->i2c, &data_status);
	if (rv) {
		LOG_ERR("Failed to read data status reg (%d)", rv);
		goto error_recovery;
	}

	/* Copy over the 5 status bytes, skipping the reg and length bytes */
	data->user_buf[0] = data_status.raw_value[2];
	data->user_buf[1] = data_status.raw_value[3];
	data->user_buf[2] = data_status.raw_value[4];
	data->user_buf[3] = data_status.raw_value[5];
	data->user_buf[4] = data_status.raw_value[6];

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static int write_task_cmd(struct pdc_config_t const *cfg,
			  enum command_task task, union reg_data *cmd_data)
{
	union reg_command cmd;
	int rv;

	cmd.command = task;

	if (cmd_data) {
		rv = tps_rw_data_for_cmd1(&cfg->i2c, cmd_data, I2C_MSG_WRITE);
		if (rv) {
			return rv;
		}
	}

	rv = tps_rw_command_for_i2c1(&cfg->i2c, &cmd, I2C_MSG_WRITE);

	return rv;
}

static void task_gaid(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = write_task_cmd(cfg, COMMAND_TASK_GAID, NULL);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_srdy(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	union reg_power_path_status pdc_power_path_status;
	int rv;
	uint32_t ext_vbus_sw;

	rv = tps_rd_power_path_status(&cfg->i2c, &pdc_power_path_status);
	if (rv) {
		LOG_ERR("Failed to power path status");
		goto error_recovery;
	}

	ext_vbus_sw = (cfg->connector_number == 0 ?
			       pdc_power_path_status.pa_ext_vbus_sw :
			       pdc_power_path_status.pb_ext_vbus_sw);
	if (data->snk_fet_en && ext_vbus_sw != EXT_VBUS_SWITCH_ENABLED_INPUT) {
		/* Enable Sink FET */
		cmd_data.data[0] = cfg->connector_number ? 0x02 : 0x03;
		rv = write_task_cmd(cfg, COMMAND_TASK_SRDY, &cmd_data);
	} else if (!data->snk_fet_en &&
		   ext_vbus_sw == EXT_VBUS_SWITCH_ENABLED_INPUT) {
		/* Disable Sink FET */
		rv = write_task_cmd(cfg, COMMAND_TASK_SRYR, NULL);
	} else {
		/* Sink already in desired state. Mark command completed */
		data->cci_event.command_completed = 1;
		/* Inform the system of the event */
		call_cci_event_cb(data);

		/* Transition to idle state */
		set_state(data, ST_IDLE);
		return;
	}

	if (rv) {
		LOG_ERR("Failed to write command");
		goto error_recovery;
	}

	/* Transition to wait state */
	set_state(data, ST_TASK_WAIT);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void task_ucsi(struct pdc_data_t *data, enum ucsi_command_t ucsi_command)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	int rv;

	memset(cmd_data.data, 0, sizeof(cmd_data.data));
	/* Byte 0: UCSI Command Code */
	cmd_data.data[0] = ucsi_command;
	/* Byte 1: Data length per UCSI spec */
	cmd_data.data[1] = 0;
	/* Connector Number: Byte 2, bits 6:0. Bit 7 is reserved */
	cmd_data.data[2] = cfg->connector_number + 1;

	/* TODO(b/345783692): The bit shifts in this function come from the
	 * awkward mapping between the structures in ucsi_v3.h and the TI
	 * command format, but this can probably be cleaned up a bit.
	 */
	switch (data->cmd) {
	case CMD_CONNECTOR_RESET:
		cmd_data.data[2] |= (data->connector_reset.reset_type << 7);
		break;
	case CMD_GET_PDOS:
		/* Partner PDO: Byte 2, bits 7 */
		cmd_data.data[2] |= (data->port_partner_pdo << 7);
		/* PDO Offset: Byte 3, bits 7:0 */
		cmd_data.data[3] = data->pdo_offset;
		/* Number of PDOs: Byte 4, bits 1:0 */
		cmd_data.data[4] = data->num_pdos;
		/* Source or Sink PDOSs: Byte 4, bits 2 */
		cmd_data.data[4] |= (data->pdo_type << 2);
		/* Source Capabilities Type: Byte 4, bits 4:3 */
		/* cmd_data.data[4] |= (0x00 << 3); */
		break;
	case CMD_SET_CCOM:
		switch (data->ccom) {
		case CCOM_RP:
			cmd_data.data[2] |= (1 << 7);
			break;
		case CCOM_RD:
			cmd_data.data[3] = 1;
			break;
		case CCOM_DRP:
			cmd_data.data[3] = 2;
			break;
		}
		break;
	case CMD_SET_UOR:
		cmd_data.data[2] |= (data->uor.swap_to_dfp << 7);
		cmd_data.data[3] = (data->uor.swap_to_ufp |
				    (data->uor.accept_dr_swap << 1));
		break;
	case CMD_SET_PDR:
		cmd_data.data[2] |= (data->pdr.swap_to_src << 7);
		cmd_data.data[3] = (data->pdr.swap_to_snk |
				    (data->pdr.accept_pr_swap << 1));
		break;
	case CMD_SET_NOTIFICATION_ENABLE:
		*(uint32_t *)&cmd_data.data[2] = cfg->bits.raw_value;
		break;
	default:
		/* Data doesn't need processed */
		break;
	}

	rv = write_task_cmd(cfg, COMMAND_TASK_UCSI, &cmd_data);
	if (rv) {
		LOG_ERR("Failed to write command");
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	/* Transition to wait state */
	set_state(data, ST_TASK_WAIT);
	return;
}

static void st_task_wait_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void st_task_wait_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_command cmd;
	union reg_data cmd_data;
	uint8_t offset;
	uint32_t len;
	int rv;

	/* Read command register for the particular port */
	rv = tps_rw_command_for_i2c1(&cfg->i2c, &cmd, I2C_MSG_READ);
	if (rv) {
		/* I2C transaction failed */
		LOG_ERR("Failed to read command");
		goto error_recovery;
	}

	/*
	 * Wait for command to complete:
	 *  1) command is set to 0 when command is sent
	 *  2) command is set to "!CMD" for unknown command
	 */
	if (cmd.command && cmd.command != COMMAND_TASK_NO_COMMAND) {
		return;
	}

	/*
	 * Read status of command for particular port:
	 *  1) cmd_data is set to zero on success
	 *  2) cmd_data is set to an error code on failure
	 */
	rv = tps_rw_data_for_cmd1(&cfg->i2c, &cmd_data, I2C_MSG_READ);
	if (rv) {
		/* I2C transaction failed */
		LOG_ERR("Failed to read command");
		goto error_recovery;
	}

	/* Data byte offset 0 is the return error code */
	if (cmd.command || cmd_data.data[0] != 0) {
		/* Command has completed with error */
		if (cmd.command == COMMAND_TASK_NO_COMMAND) {
			LOG_DBG("Command %d not supported", data->cmd);
		} else {
			LOG_DBG("Command %d failed. Err : %d", data->cmd,
				cmd_data.data[0]);
		}
		data->cci_event.error = 1;
	}

	switch (data->cmd) {
	case CMD_GET_CONNECTOR_CAPABILITY:
		offset = 1;
		len = sizeof(union connector_capability_t);
		break;
	case CMD_GET_CONNECTOR_STATUS:
		offset = 1;
		len = sizeof(union connector_status_t);
		/* TODO(b/345783692): Cache result */
		break;
	case CMD_GET_CABLE_PROPERTY:
		offset = 1;
		len = sizeof(union cable_property_t);
		break;
	case CMD_GET_ERROR_STATUS:
		offset = 2;
		len = cmd_data.data[1];
		break;
	case CMD_GET_PDOS: {
		len = cmd_data.data[1];
		offset = 2;
		break;
	}
	default:
		/* No data for this command */
		len = 0;
	}

	if (data->user_buf && len) {
		if (data->cci_event.error) {
			memset(data->user_buf, 0, len);
		} else {
			/* No preprocessing needed for the user data */
			memcpy(data->user_buf, &cmd_data.data[offset], len);
		}
	}

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle state */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

/* Populate state table */
static const struct smf_state states[] = {
	[ST_IRQ] = SMF_CREATE_STATE(st_irq_entry, st_irq_run, NULL, NULL, NULL),
	[ST_INIT] = SMF_CREATE_STATE(st_init_entry, st_init_run, st_init_exit,
				     NULL, NULL),
	[ST_IDLE] = SMF_CREATE_STATE(st_idle_entry, st_idle_run, st_idle_exit,
				     NULL, NULL),
	[ST_ERROR_RECOVERY] = SMF_CREATE_STATE(st_error_recovery_entry,
					       st_error_recovery_run, NULL,
					       NULL, NULL),
	[ST_TASK_WAIT] = SMF_CREATE_STATE(st_task_wait_entry, st_task_wait_run,
					  NULL, NULL, NULL),
	[ST_SUSPENDED] = SMF_CREATE_STATE(st_suspended_entry, st_suspended_run,
					  NULL, NULL, NULL),
};

static int tps_post_command(const struct device *dev, enum cmd_t cmd,
			    void *user_buf)
{
	struct pdc_data_t *data = dev->data;

	/* TODO(b/345783692): Double check this logic. */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	if (k_mutex_lock(&data->mtx, K_MSEC(100)) == 0) {
		if (data->cmd != CMD_NONE) {
			k_mutex_unlock(&data->mtx);
			return -EBUSY;
		}

		data->user_buf = user_buf;
		data->cmd = cmd;

		k_mutex_unlock(&data->mtx);
		k_event_post(&data->pdc_event, PDC_CMD_EVENT);
	} else {
		return -EBUSY;
	}

	return 0;
}

static int tps_manage_callback(const struct device *dev,
			       struct pdc_callback *callback, bool set)
{
	struct pdc_data_t *const data = dev->data;

	return pdc_manage_callbacks(&data->ci_cb_list, callback, set);
}

static int tps_ack_cc_ci(const struct device *dev,
			 union conn_status_change_bits_t ci, bool cc,
			 uint16_t vendor_defined)
{
	struct pdc_data_t *data = dev->data;

	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* TODO(b/345783692): Implement */

	return 0;
}

static int tps_get_ucsi_version(const struct device *dev, uint16_t *version)
{
	if (version == NULL) {
		return -EINVAL;
	}

	*version = UCSI_VERSION;

	return 0;
}

static int tps_set_handler_cb(const struct device *dev,
			      struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	data->cc_cb = callback;

	return 0;
}

static int tps_read_power_level(const struct device *dev)
{
	return tps_post_command(dev, CMD_READ_POWER_LEVEL, NULL);
}

static int tps_reconnect(const struct device *dev)
{
	/* TODO */
	return 0;
}

static int tps_pdc_reset(const struct device *dev)
{
	return tps_post_command(dev, CMD_TRIGGER_PDC_RESET, NULL);
}

static int tps_connector_reset(const struct device *dev,
			       union connector_reset_t type)
{
	struct pdc_data_t *data = dev->data;

	data->connector_reset = type;

	return tps_post_command(dev, CMD_CONNECTOR_RESET, NULL);
}

static int tps_set_power_level(const struct device *dev,
			       enum usb_typec_current_t tcc)
{
	struct pdc_data_t *data = dev->data;

	data->tcc = tcc;

	return tps_post_command(dev, CMD_SET_TPC_RP, NULL);
}

static int tps_set_sink_path(const struct device *dev, bool en)
{
	struct pdc_data_t *data = dev->data;

	data->snk_fet_en = en;

	return tps_post_command(dev, CMD_SET_SINK_PATH, NULL);
}

static int tps_get_capability(const struct device *dev,
			      struct capability_t *caps)
{
	return tps_post_command(dev, CMD_GET_CAPABILITY, (uint8_t *)caps);
}

static int tps_get_connector_capability(const struct device *dev,
					union connector_capability_t *caps)
{
	return tps_post_command(dev, CMD_GET_CONNECTOR_CAPABILITY, caps);
}

static int tps_get_connector_status(const struct device *dev,
				    union connector_status_t *cs)
{
	return tps_post_command(dev, CMD_GET_CONNECTOR_STATUS, cs);
}

static int tps_get_error_status(const struct device *dev,
				union error_status_t *es)
{
	return tps_post_command(dev, CMD_GET_ERROR_STATUS, es);
}

static int tps_set_rdo(const struct device *dev, uint32_t rdo)
{
	/* TODO */
	return 0;
}

static int tps_get_rdo(const struct device *dev, uint32_t *rdo)
{
	return tps_post_command(dev, CMD_GET_RDO, rdo);
}

static int tps_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			bool port_partner_pdo, uint32_t *pdos)
{
	struct pdc_data_t *data = dev->data;

	/* TODO(b/345783692): Make sure these accesses don't need to be
	 * synchronized.
	 */

	data->pdo_type = pdo_type;
	data->pdo_offset = pdo_offset;
	data->num_pdos = num_pdos;
	data->port_partner_pdo = port_partner_pdo;

	return tps_post_command(dev, CMD_GET_PDOS, pdos);
}

static int tps_set_pdos(const struct device *dev, enum pdo_type_t type,
			uint32_t *pdo, int count)
{
	struct pdc_data_t *data = dev->data;

	data->pdo_type = type;
	data->pdos = pdo;
	data->num_pdos = count;

	return tps_post_command(dev, CMD_SET_PDOS, NULL);
}

static int tps_get_info(const struct device *dev, struct pdc_info_t *info,
			bool live)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;

	if (info == NULL) {
		return -EINVAL;
	}

	/* If caller is OK with a non-live value and we have one, we can
	 * immediately return a cached value. (synchronous)
	 */
	if (live == false) {
		k_mutex_lock(&data->mtx, K_FOREVER);

		/* Check FW ver for valid value to ensure we have a resident
		 * value.
		 */
		if (data->info.fw_version == PDC_FWVER_INVALID) {
			k_mutex_unlock(&data->mtx);

			/* No cached value. Caller should request a live read */
			return -EAGAIN;
		}

		*info = data->info;
		k_mutex_unlock(&data->mtx);

		LOG_DBG("DR%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(data->info.fw_version),
			PDC_FWVER_GET_MINOR(data->info.fw_version),
			PDC_FWVER_GET_PATCH(data->info.fw_version));
		return 0;
	}

	/* Perform a live read (async) */
	return tps_post_command(dev, CMD_GET_IC_STATUS, info);
}

static int tps_get_bus_info(const struct device *dev,
			    struct pdc_bus_info_t *info)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;

	if (info == NULL) {
		return -EINVAL;
	}

	info->bus_type = PDC_BUS_TYPE_I2C;
	info->i2c = cfg->i2c;

	return 0;
}

static int tps_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	return tps_post_command(dev, CMD_GET_VBUS_VOLTAGE, voltage);
}

static int tps_set_ccom(const struct device *dev, enum ccom_t ccom)
{
	struct pdc_data_t *data = dev->data;

	data->ccom = ccom;

	return tps_post_command(dev, CMD_SET_CCOM, NULL);
}

static int tps_set_uor(const struct device *dev, union uor_t uor)
{
	struct pdc_data_t *data = dev->data;

	data->uor = uor;

	return tps_post_command(dev, CMD_SET_UOR, NULL);
}

static int tps_set_pdr(const struct device *dev, union pdr_t pdr)
{
	struct pdc_data_t *data = dev->data;

	data->pdr = pdr;

	return tps_post_command(dev, CMD_SET_PDR, NULL);
}

static int tps_set_drp_mode(const struct device *dev, enum drp_mode_t dm)
{
	struct pdc_data_t *data = dev->data;

	data->drp_mode = dm;

	return tps_post_command(dev, CMD_SET_DRP_MODE, NULL);
}

static int tps_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	/* TODO */
	return 0;
}

static int tps_get_cable_property(const struct device *dev,
				  union cable_property_t *cp)
{
	if (cp == NULL) {
		return -EINVAL;
	}

	return tps_post_command(dev, CMD_GET_CABLE_PROPERTY, cp);
}

static int tps_get_vdo(const struct device *dev, union get_vdo_t vdo_req,
		       uint8_t *vdo_req_list, uint32_t *vdo)
{
	struct pdc_data_t *data = dev->data;

	if (vdo == NULL || vdo_req_list == NULL) {
		return -EINVAL;
	}

	for (int i = 0; i < vdo_req.num_vdos; i++) {
		data->vdo_req_list[i] = vdo_req_list[i];
	}
	data->vdo_req = vdo_req;

	return tps_post_command(dev, CMD_GET_VDO, vdo);
}

static int tps_get_identity_discovery(const struct device *dev,
				      bool *disc_state)
{
	if (disc_state == NULL) {
		return -EINVAL;
	}

	return tps_post_command(dev, CMD_GET_IDENTITY_DISCOVERY, disc_state);
}

static int tps_set_comms_state(const struct device *dev, bool comms_active)
{
	struct pdc_data_t *data = dev->data;

	if (comms_active) {
		/* Re-enable communications. Clearing the suspend flag will
		 * trigger a reset. Note: if the driver is in the disabled
		 * state due to a previous comms failure, it will remain
		 * disabled. (Thus, suspending/resuming comms on a disabled
		 * PDC driver is a no-op)
		 */
		enable_comms();

	} else {
		/** Allow 3 seconds for the driver to suspend itself. */
		const int suspend_timeout_usec = 3 * USEC_PER_SEC;

		/* Request communication to be stopped. This allows in-progress
		 * operations to complete first.
		 */
		suspend_comms();

		/* Signal the driver with the suspend request event in case the
		 * thread is blocking on an event to process.
		 */
		k_event_post(&data->pdc_event, PDC_CMD_SUSPEND_REQUEST_EVENT);

		/* Wait for driver to enter the suspended state */
		if (!WAIT_FOR((get_state(data) == ST_SUSPENDED),
			      suspend_timeout_usec, k_sleep(K_MSEC(50)))) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static bool tps_is_init_done(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	return data->init_done;
}

static int tps_get_pch_data_status(const struct device *dev, uint8_t port_num,
				   uint8_t *status_reg)
{
	ARG_UNUSED(port_num);

	if (status_reg == NULL) {
		return -EINVAL;
	}

	return tps_post_command(dev, CMD_GET_PCH_DATA_STATUS, status_reg);
}

static const struct pdc_driver_api_t pdc_driver_api = {
	.is_init_done = tps_is_init_done,
	.get_ucsi_version = tps_get_ucsi_version,
	.reset = tps_pdc_reset,
	.connector_reset = tps_connector_reset,
	.get_capability = tps_get_capability,
	.get_connector_capability = tps_get_connector_capability,
	.set_ccom = tps_set_ccom,
	.set_uor = tps_set_uor,
	.set_pdr = tps_set_pdr,
	.set_drp_mode = tps_set_drp_mode,
	.set_sink_path = tps_set_sink_path,
	.get_connector_status = tps_get_connector_status,
	.get_pdos = tps_get_pdos,
	.set_pdos = tps_set_pdos,
	.get_rdo = tps_get_rdo,
	.set_rdo = tps_set_rdo,
	.get_error_status = tps_get_error_status,
	.get_vbus_voltage = tps_get_vbus_voltage,
	.get_current_pdo = tps_get_current_pdo,
	.set_handler_cb = tps_set_handler_cb,
	.read_power_level = tps_read_power_level,
	.get_info = tps_get_info,
	.get_bus_info = tps_get_bus_info,
	.set_power_level = tps_set_power_level,
	.reconnect = tps_reconnect,
	.get_cable_property = tps_get_cable_property,
	.get_vdo = tps_get_vdo,
	.get_identity_discovery = tps_get_identity_discovery,
	.manage_callback = tps_manage_callback,
	.ack_cc_ci = tps_ack_cc_ci,
	.set_comms_state = tps_set_comms_state,
	.get_pch_data_status = tps_get_pch_data_status,
};

static int pdc_interrupt_mask_init(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt irq_mask = {
		.pd_hardreset = 1,
		.plug_insert_or_removal = 1,
		.power_swap_complete = 1,
		.fr_swap_complete = 1,
		.data_swap_complete = 1,
		.status_updated = 1,
		.power_event_occurred_error = 1,
		.externl_dcdc_event_received = 1,
	};

	return tps_rw_interrupt_mask(&cfg->i2c, &irq_mask, I2C_MSG_WRITE);
}

static void pdc_interrupt_callback(const struct device *dev,
				   struct gpio_callback *cb, uint32_t pins)
{
	/* All ports share a common interrupt, so post a PDC_IRQ_EVENT to all
	 * drivers. The driver IRQ state will determine if it has a pending
	 * interrupt */
	for (int i = 0; i < NUM_PDC_TPS6699X_PORTS; i++) {
		k_event_post(&pdc_data[i]->pdc_event, PDC_IRQ_EVENT);
	}
}

static int pdc_init(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	int rv;

	rv = i2c_is_ready_dt(&cfg->i2c);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	rv = gpio_is_ready_dt(&cfg->irq_gpios);
	if (rv < 0) {
		LOG_ERR("device %s not ready", cfg->irq_gpios.port->name);
		return -ENODEV;
	}

	k_event_init(&data->pdc_event);
	k_mutex_init(&data->mtx);

	data->cmd = CMD_NONE;
	data->dev = dev;
	pdc_data[cfg->connector_number] = data;
	data->init_done = false;

	rv = gpio_pin_configure_dt(&cfg->irq_gpios, GPIO_INPUT);
	if (rv < 0) {
		LOG_ERR("Unable to configure GPIO");
		return rv;
	}

	gpio_init_callback(&data->gpio_cb, pdc_interrupt_callback,
			   BIT(cfg->irq_gpios.pin));

	rv = gpio_add_callback(cfg->irq_gpios.port, &data->gpio_cb);
	if (rv < 0) {
		LOG_ERR("Unable to add callback");
		return rv;
	}

	rv = gpio_pin_interrupt_configure_dt(&cfg->irq_gpios,
					     GPIO_INT_EDGE_FALLING);
	if (rv < 0) {
		LOG_ERR("Unable to configure interrupt");
		return rv;
	}

	/* Set initial state */
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	/* Setup I2C1 interrupt mask for this port */
	rv = pdc_interrupt_mask_init(data);
	if (rv < 0) {
		LOG_ERR("Write interrupt mask failed");
		return rv;
	}

	/* Trigger an interrupt on startup */
	k_event_post(&data->pdc_event, PDC_IRQ_EVENT);

	LOG_INF("TI TPS6699X PDC DRIVER FOR PORT %d", cfg->connector_number);

	return 0;
}

/* LCOV_EXCL_START - temporary code */
#ifdef CONFIG_USBC_PDC_TPS6699X_FW_UPDATER
/* See tps6699x_fwup.c */
extern int tps6699x_do_firmware_update_internal(const struct i2c_dt_spec *dev);

int tps_pdc_do_firmware_update(void)
{
	/* Get DT node for first PDC port */
	const struct device *dev = DEVICE_DT_GET(DT_INST(0, DT_DRV_COMPAT));
	const struct pdc_config_t *cfg = dev->config;

	return tps6699x_do_firmware_update_internal(&cfg->i2c);
}
#endif /* CONFIG_USBC_PDC_TPS6699X_FW_UPDATER */
/* LCOV_EXCL_STOP - temporary code */

static void tps_thread(void *dev, void *unused1, void *unused2)
{
	struct pdc_data_t *data = ((const struct device *)dev)->data;

	while (1) {
		smf_run_state(SMF_CTX(data));
		/* TODO(b/345783692): Consider waiting for an event with a
		 * timeout to avoid high interrupt-handling latency.
		 */
		k_sleep(K_MSEC(50));
	}
}

#define PDC_DEFINE(inst)                                                       \
	K_THREAD_STACK_DEFINE(thread_stack_area_##inst,                        \
			      CONFIG_USBC_PDC_TPS6699X_STACK_SIZE);            \
                                                                               \
	static void create_thread_##inst(const struct device *dev)             \
	{                                                                      \
		struct pdc_data_t *data = dev->data;                           \
                                                                               \
		data->thread = k_thread_create(                                \
			&data->thread_data, thread_stack_area_##inst,          \
			K_THREAD_STACK_SIZEOF(thread_stack_area_##inst),       \
			tps_thread, (void *)dev, 0, 0,                         \
			CONFIG_USBC_PDC_TPS6699X_THREAD_PRIORITY, K_ESSENTIAL, \
			K_NO_WAIT);                                            \
		k_thread_name_set(data->thread, "TPS6699X" STRINGIFY(inst));   \
	}                                                                      \
                                                                               \
	static struct pdc_data_t pdc_data_##inst;                              \
                                                                               \
	/* TODO(b/345783692): Make sure interrupt enable bits match the events \
	 * we need to respond to.                                              \
	 */                                                                    \
	static const struct pdc_config_t pdc_config##inst = {                  \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                             \
		.irq_gpios = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
		.connector_number =                                            \
			USBC_PORT_FROM_DRIVER_NODE(DT_DRV_INST(inst), pdc),    \
		.bits.command_completed = 0, /* Reserved on TI */              \
		.bits.external_supply_change = 1,                              \
		.bits.power_operation_mode_change = 1,                         \
		.bits.attention = 0,                                           \
		.bits.fw_update_request = 0,                                   \
		.bits.provider_capability_change_supported = 1,                \
		.bits.negotiated_power_level_change = 1,                       \
		.bits.pd_reset_complete = 1,                                   \
		.bits.support_cam_change = 1,                                  \
		.bits.battery_charging_status_change = 1,                      \
		.bits.security_request_from_port_partner = 0,                  \
		.bits.connector_partner_change = 1,                            \
		.bits.power_direction_change = 1,                              \
		.bits.set_retimer_mode = 0,                                    \
		.bits.connect_change = 1,                                      \
		.bits.error = 1,                                               \
		.bits.sink_path_status_change = 1,                             \
		.create_thread = create_thread_##inst,                         \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL, &pdc_data_##inst,          \
			      &pdc_config##inst, POST_KERNEL,                  \
			      CONFIG_APPLICATION_INIT_PRIORITY,                \
			      &pdc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PDC_DEFINE)
