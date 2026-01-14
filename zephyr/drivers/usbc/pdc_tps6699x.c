/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TI TPS6699X Power Delivery Controller Driver
 */

#include <assert.h>
#include <stdio.h>
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
#include "usbc/pdc_utils.h"
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
/** @brief Trigger internal event to wake up (or keep awake) thread to handle
 *         requests */
#define PDC_INTERNAL_EVENT BIT(3)
/** @brief Trigger thread to send command complete back to
 *         PDC Power Mgmt thread */
#define PDC_CMD_COMPLETE_EVENT BIT(4)
/** @brief Bit mask of all PDC events */
#define PDC_ALL_EVENTS BIT_MASK(5)

/** @brief Time between checking TI CMDx register for data ready */
#define PDC_TI_DATA_READY_TIME_MS (10)

/** @brief Delay after "New Contract as Consumer" interrupt bit set that the
 * TPS6699x will accept SRDY to enable the sink path. See b/358274846.
 */
#define PDC_TI_NEW_POWER_CONTRACT_DELAY_MS (5)
/** @brief Delay after SET_SINK_PATH is called without any active power
 * contract. If there is no new power contract after this delay, return an error
 * on SET_SINK_PATH.
 */
#define PDC_TI_SET_SINK_PATH_DELAY_MS (1000)

/* When initializing, delay entering error recovery to give PDC time to fully
 * init and be responsive on i2c.
 */
#define PDC_INIT_ERROR_RECOVERY_DELAY_MS (250)

/* After executing GAID, the system is typically unavailable for 1s as the
 * system resets according to the reference manual. However, it has been seen to
 * be upwards of 2s when doing firmware update. The delay here is a value less
 * than the state machine timeout in |pdc_power_mgmt.c| but greater than the
 * minimum 1s as described in the reference manual (based on experimentation).
 */
#define PDC_TI_GAID_DELAY_MS (1600)

/* Error recovery period for handling interrupts (i.e. tried to read interrupt
 * registers but failed).
 */
#define PDC_HANDLE_IRQ_RETRY_DELAY (50)

/** @brief The number of times to try to initialize the driver before quitting.
 */
#define PDC_INIT_RETRY_MAX 3

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

/* Make sure pdc_info_t::project_name has enough space for the config identifier
 * string stored in the customer-use register plus a NUL-terminator byte.
 */
BUILD_ASSERT(sizeof(((union reg_customer_use *)0)->data) + 1 <=
	     sizeof(((struct pdc_info_t *)0)->project_name));

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
	/** Set RDO */
	CMD_SET_RDO,
	/** Set Sink Path */
	CMD_SET_SINK_PATH,
	/** Get current Partner SRC PDO */
	CMD_GET_CURRENT_PARTNER_SRC_PDO,
	/** Set the Rp TypeC current */
	CMD_SET_TPC_RP,
	/** Set Fast Role Swap */
	CMD_SET_FRS,
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
	/** CMD_GET_DRP_MODE */
	CMD_GET_DRP_MODE,
	/** CMD_UPDATE_RETIMER */
	CMD_UPDATE_RETIMER,
	/** CMD_RECONNECT */
	CMD_RECONNECT,
	/** CMD_GET_CURRENT_PDO */
	CMD_GET_CURRENT_PDO,
	/** CMD_IS_VCONN_SOURCING */
	CMD_IS_VCONN_SOURCING,
	/** CMD_GET_SBU_MUX_MODE */
	CMD_GET_SBU_MUX_MODE,
	/** CMD_SET_SBU_MUX_MODE */
	CMD_SET_SBU_MUX_MODE,
	/** CMD_RAW_UCSI */
	CMD_RAW_UCSI,
	/** Set data role swap options */
	CMD_SET_DRS,
	/** Set Sx App Config register (AP power state) */
	CMD_SET_SX_APP_CONFIG,
	/** Set intel retimer compliance mode */
	CMD_SET_BBR_CTS,
	/** Get attention VDO */
	CMD_GET_ATTENTION_VDO,
};

/**
 * @brief States of the main state machine
 */
enum state_t {
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
	/** If true, do not apply PDC FW updates to this port */
	bool no_fw_update;
	/** Whether or not this port supports CCD */
	bool ccd;
	/** The index of the port on chip */
	uint8_t port_index_on_chip;
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
	/* Init attempt counter */
	int init_attempt;
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
	enum port_control_typec_current_t tcc;
	/** Fast Role Swap flag */
	bool fast_role_swap;
	/** Sink FET enable */
	bool snk_fet_en;
	/** retimer feature enable */
	bool retimer_feature_en;
	/** Connector reset type */
	union connector_reset_t connector_reset;
	/** PDO Type */
	enum pdo_type_t pdo_type;
	/** PDO Offset */
	enum pdo_offset_t pdo_offset;
	/** Number of PDOS */
	uint8_t num_pdos;
	/** PDO storage for command processing */
	uint32_t pdos[PDO_MAX_OBJECTS];
	/** Port Partner PDO */
	enum pdo_source_t pdo_source;
	/** Cached PDOS */
	uint32_t cached_pdos[PDO_MAX_OBJECTS];
	/** RDO */
	uint32_t rdo;
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
	/** UCSI command valid in |task_wait| state or 0 if vendor cmd */
	enum ucsi_command_t running_ucsi_cmd;
	/* VDO request list */
	enum vdo_type_t vdo_req_list[8];
	/* Request VDO */
	union get_vdo_t vdo_req;
	/* PDC event: Interrupt or Command */
	struct k_event pdc_event;
	/* Events to be processed */
	uint32_t events;
	/* Deferred handler to trigger event to check if data is ready */
	struct k_work_delayable data_ready;
	/* Deferred handler to trigger event when new contract has been stable
	 * long enough that PDC should accept SRDY.
	 */
	struct k_work_delayable new_power_contract;
	/* Deferred handler to trigger internal event. Used by
	 * set_state_delayed_post.
	 * */
	struct k_work_delayable delayed_post;
	/* Set when aNEG may be used. */
	atomic_t set_rdo_possible;
	/* Set when SRDY may be used. */
	atomic_t sink_enable_possible;
	/* CMD to send to PDC from tps_notify_new_power_contract */
	enum cmd_t delayable_cmd;
	/* Should use cached connector status change bits */
	bool use_cached_conn_status_change;
	/* Cached connector status for this connector. */
	union connector_status_t cached_conn_status;
	/* sbumux mode */
	enum pdc_sbu_mux_mode sbumux_mode;
	/* Raw UCSI data to send. */
	union reg_data raw_ucsi_cmd_data;
	/* Current AP power state */
	uint8_t sx_state;
};

/**
 * @brief List of human readable state names for console debugging
 */
static const char *const state_names[] = {
	[ST_INIT] = "INIT",
	[ST_IDLE] = "IDLE",
	[ST_ERROR_RECOVERY] = "ERROR RECOVERY",
	[ST_TASK_WAIT] = "TASK_WAIT",
	[ST_SUSPENDED] = "SUSPENDED",
};

static const struct smf_state states[];

static void cmd_set_drp_mode(struct pdc_data_t *data);
static void cmd_set_drs(struct pdc_data_t *data);
static void cmd_get_drp_mode(struct pdc_data_t *data);
static void cmd_set_tpc_rp(struct pdc_data_t *data);
static void cmd_set_frs(struct pdc_data_t *data);
static void cmd_get_rdo(struct pdc_data_t *data);
static void cmd_set_rdo(struct pdc_data_t *data);
static void cmd_get_ic_status(struct pdc_data_t *data);
static int cmd_get_ic_status_sync_internal(struct pdc_config_t const *cfg,
					   struct pdc_info_t *info);
static void cmd_get_vdo(struct pdc_data_t *data);
static void cmd_get_identity_discovery(struct pdc_data_t *data);
static void cmd_get_pdc_data_status_reg(struct pdc_data_t *data);
static void cmd_get_sbu_mux_mode(struct pdc_data_t *data);
static void cmd_update_retimer(struct pdc_data_t *data);
static void cmd_get_current_pdo(struct pdc_data_t *data);
static void cmd_is_vconn_sourcing(struct pdc_data_t *data);
static void cmd_set_sx_app_config(struct pdc_data_t *data);
static void cmd_get_attention_vdo(struct pdc_data_t *data);
static void task_trig(struct pdc_data_t *data);
static void task_gaid(struct pdc_data_t *data);
static void task_srdy(struct pdc_data_t *data);
static void task_dbfg(struct pdc_data_t *data);
static void task_aneg(struct pdc_data_t *data);
static void task_disc(struct pdc_data_t *data);
static void task_sbud(struct pdc_data_t *data);
static void task_ucsi(struct pdc_data_t *data,
		      enum ucsi_command_t ucsi_command);
static void task_raw_ucsi(struct pdc_data_t *data);
static int pdc_autonegotiate_sink_reset(struct pdc_data_t *data);
static void tps_check_and_notify_irq(void);

/**
 * @brief PDC port data used in interrupt handler
 */
static struct pdc_data_t *const pdc_data[NUM_PDC_TPS6699X_PORTS];

static inline bool is_first_port_on_chip(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	return cfg->port_index_on_chip == 1;
}

static enum state_t get_state(struct pdc_data_t *data)
{
	return data->ctx.current - &states[0];
}

static void set_state(struct pdc_data_t *data, const enum state_t next_state)
{
	/* Make sure the run functions are executed for these states
	 * on transitions.
	 */
	switch (next_state) {
	case ST_INIT:
	case ST_TASK_WAIT:
	case ST_ERROR_RECOVERY:
	case ST_SUSPENDED:
		k_event_post(&data->pdc_event, PDC_INTERNAL_EVENT);
		break;
	default:
		break;
	}
	smf_set_state(SMF_CTX(data), &states[next_state]);
}

/* Immediately set the state but delay posting an event for the state.
 */
static void set_state_delayed_post(struct pdc_data_t *data,
				   const enum state_t next_state,
				   const int delay_ms)
{
	smf_set_state(SMF_CTX(data), &states[next_state]);
	k_work_reschedule(&data->delayed_post, K_MSEC(delay_ms));
}

static void tps_delayed_post(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pdc_data_t *data =
		CONTAINER_OF(dwork, struct pdc_data_t, delayed_post);

	k_event_post(&data->pdc_event, PDC_INTERNAL_EVENT);
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
	enum state_t s = get_state(data);

	if (s == ST_IDLE || s == ST_TASK_WAIT) {
		LOG_DBG("TI%d: %s", cfg->connector_number, state_names[s]);
	} else {
		LOG_INF("TI%d: %s", cfg->connector_number, state_names[s]);
	}
}

static void call_cci_event_cb(struct pdc_data_t *data)
{
	const struct pdc_config_t *cfg = data->dev->config;
	const union cci_event_t cci = data->cci_event;

	LOG_INF("TI%d: CCI=0x%x", cfg->connector_number, cci.raw_value);

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

static void tps_notify_new_power_contract(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pdc_data_t *data =
		CONTAINER_OF(dwork, struct pdc_data_t, new_power_contract);

	/* If we're not currently idle, nothing to do. */
	if (get_state(data) != ST_IDLE) {
		return;
	}

	/* If we were attempting to run CMD_SET_SINK_PATH, re-trigger the
	 * command execution.
	 *
	 * This task gets scheduled for only two reasons:
	 * - New Power Contract interrupt is seen (setting sink_enable_possible
	 *   to true)
	 * - Previous SET_SINK_PATH attempt timed out before seeing new
	 *   contract.
	 */
	if (data->delayable_cmd == CMD_SET_SINK_PATH) {
		atomic_set(&data->sink_enable_possible, 1);
		/* Safe now to send CMD_SET_SINK_PATH */
		data->cmd = CMD_SET_SINK_PATH;
		k_event_post(&data->pdc_event, PDC_CMD_EVENT);
		data->delayable_cmd = CMD_NONE;
	} else if (data->delayable_cmd == CMD_SET_RDO) {
		atomic_set(&data->set_rdo_possible, 1);
		/* Safe now to send CMD_SET_RDO */
		data->cmd = CMD_SET_RDO;
		k_event_post(&data->pdc_event, PDC_CMD_EVENT);
		data->delayable_cmd = CMD_NONE;
	}
}

static int pdc_interrupt_mask_init(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt irq_mask = {
		.pd_hardreset = 1,
		.plug_insert_or_removal = 1,
		.power_swap_complete = 1,
		.fr_swap_complete = 1,
		.data_swap_complete = 1,
		.sink_ready = 1,
		.attention_received = 1,
		.new_contract_as_consumer = 1,
		.ucsi_connector_status_change_notification = 1,
		.power_event_occurred_error = 1,
		.externl_dcdc_event_received = 1,
		.patch_loaded = 1,
	};

	return tps_rw_interrupt_mask(&cfg->i2c, &irq_mask, I2C_MSG_WRITE);
}

static int pdc_port_control_init(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_control pdc_port_control = {
		.typec_current = 1,
		.process_swap_to_sink = 1,
		.process_swap_to_source = 1,
		.automatic_cap_request = 1,
		.auto_alert_enable = 1,
		.process_swap_to_dfp = 1,
		.automatic_id_request = 1,
		.fr_swap_enabled = 1,
		.deglitch_cnt_lo = 6,
	};

	return tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
}

static int pdc_autonegotiate_sink_reset(struct pdc_data_t *data)
{
	union reg_autonegotiate_sink an_snk;
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = tps_rw_autonegotiate_sink(&cfg->i2c, &an_snk, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Failed to read auto negotiate sink register.",
			cfg->connector_number);
		return rv;
	}

	an_snk.auto_compute_sink_min_power = 0;
	an_snk.auto_compute_sink_min_voltage = 0;
	an_snk.auto_compute_sink_max_voltage = 0;
	an_snk.auto_neg_max_current = 3000 / 10;
	an_snk.auto_neg_sink_min_required_power = 15000 / 250;
	an_snk.auto_neg_max_voltage = 5000 / 50;
	an_snk.auto_neg_min_voltage = 5000 / 50;

	rv = tps_rw_autonegotiate_sink(&cfg->i2c, &an_snk, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Failed to write auto negotiate sink register.",
			cfg->connector_number);
		return rv;
	}

	return 0;
}

static void set_all_ports_to_init(const int delay_ms)
{
	for (int port = 0; port < NUM_PDC_TPS6699X_PORTS; port++) {
		if (pdc_data[port] == NULL ||
		    !device_is_ready(pdc_data[port]->dev)) {
			/* Port is not in use. Skip it. */
			continue;
		}

		pdc_data[port]->init_done = false;
		pdc_data[port]->init_attempt = 0;
		if (delay_ms) {
			set_state_delayed_post(pdc_data[port], ST_INIT,
					       delay_ms);
		} else {
			set_state(pdc_data[port], ST_INIT);
		}
	}
}

static int pdc_exit_dead_battery(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_boot_flags pdc_boot_flags;
	int rv;

	rv = tps_rd_boot_flags(&cfg->i2c, &pdc_boot_flags);
	if (rv) {
		LOG_ERR("TI%d: Read boot flags failed (%d)",
			cfg->connector_number, rv);
		set_state(data, ST_ERROR_RECOVERY);
		return rv;
	}

	if (pdc_boot_flags.dead_battery_flag) {
		task_dbfg(data);
	}
	return 0;
}

static int handle_irqs(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt pdc_interrupt;
	int rv;
	int i;
	bool interrupt_pending = false;

	/* Read the pending interrupt events */
	rv = tps_rd_interrupt_event(&cfg->i2c, &pdc_interrupt);
	if (rv) {
		LOG_ERR("TI%d: Read interrupt events failed (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	/* All raw_value data uses byte-0 for contains the register data was
	 * written too, or read from, and byte-1 contains the length of said
	 * data. The actual data starts at index 2. */
	LOG_DBG("TI%d: IRQ", cfg->connector_number);
	for (i = 0; i < sizeof(union reg_interrupt); i++) {
		LOG_DBG("TI%d: Byte%d: %02x", cfg->connector_number, i,
			pdc_interrupt.raw_value[i]);
		if (pdc_interrupt.raw_value[i]) {
			interrupt_pending = true;
		}
	}
	LOG_DBG("\n");

	if (interrupt_pending && pdc_interrupt.patch_loaded) {
		/* patch_loaded is a shared interrupt bit which is not cleared
		 * individually so set ST_INIT state to all ports to avoid
		 * clearing it before handling irq on other ports. */
		set_all_ports_to_init(/*delay_ms=*/0);
		return 0;
	}

	if (!interrupt_pending) {
		return 0;
	}

	/* Set CCI EVENT for not supported */
	data->cci_event.not_supported = pdc_interrupt.not_supported_received;

	/* Set CCI EVENT for vendor defined indicator (informs subsystem
	 * that an interrupt occurred */
	data->cci_event.vendor_defined_indicator = 1;

	/* If a UCSI event is seen, stop using the cached connector
	 * status change bits and re-read from PDC and set CCI_EVENT for
	 * connector change.
	 */
	if (pdc_interrupt.ucsi_connector_status_change_notification) {
		data->use_cached_conn_status_change = false;
		data->cci_event.connector_change = cfg->connector_number + 1;
	}

	if (pdc_interrupt.plug_insert_or_removal) {
		atomic_set(&data->set_rdo_possible, 0);
		atomic_set(&data->sink_enable_possible, 0);
	}

	if (pdc_interrupt.sink_ready) {
		atomic_set(&data->set_rdo_possible, 1);
		k_work_reschedule(&data->new_power_contract,
				  K_MSEC(PDC_TI_NEW_POWER_CONTRACT_DELAY_MS));
	}

	if (pdc_interrupt.new_contract_as_consumer) {
		atomic_set(&data->sink_enable_possible, 1);
		k_work_reschedule(&data->new_power_contract,
				  K_MSEC(PDC_TI_NEW_POWER_CONTRACT_DELAY_MS));
	}

	/* TODO(b/345783692): Handle other interrupt bits. */

	/* Clear the pending interrupt events */
	rv = tps_rw_interrupt_clear(&cfg->i2c, &pdc_interrupt, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Clear interrupt events failed (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	/* Inform the subsystem of the event */
	call_cci_event_cb(data);

	/*
	 * Check if interrupt is still active from any of the ports.
	 * It's possible that the PDC will set another bit in the
	 * interrupt status register of any of the port between the time
	 * when EC reads this register and clears these status bits
	 * above. If there is still another interrupt pending, then the
	 * interrupt line will still be active.
	 */
	tps_check_and_notify_irq();

	return 0;
}

static void st_init_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Init is restarted. */
	data->init_done = false;
	data->init_attempt++;

	print_current_state(data);
}

static enum smf_state_result st_init_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_interrupt pdc_interrupt = { .patch_loaded = 1 };
	int rv;

	/* Do not start executing commands if suspended */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	/* If we've attempted init too many times, suspend instead. */
	if (data->init_attempt > PDC_INIT_RETRY_MAX) {
		suspend_comms();
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	/* We won't see patch_loaded is asserted while handing the IRQ later on
	 * if boot from dead battery as it is cleared here. */
	rv = tps_rw_interrupt_clear(&cfg->i2c, &pdc_interrupt, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Clear patch_loaded bit failed (%d)",
			cfg->connector_number, rv);
	}

	/* Pre-fetch PDC chip info and save it in the driver struct */
	rv = cmd_get_ic_status_sync_internal(cfg, &data->info);
	if (rv) {
		LOG_ERR("TI%d: Cannot obtain initial chip info (%d)",
			cfg->connector_number, rv);
		goto error;
	}

	LOG_INF("TI%d: FW Version %u.%u.%u, config='%s' (flash=%d)",
		cfg->connector_number,
		PDC_FWVER_GET_MAJOR(data->info.fw_version),
		PDC_FWVER_GET_MINOR(data->info.fw_version),
		PDC_FWVER_GET_PATCH(data->info.fw_version),
		data->info.project_name, data->info.is_running_flash_code);

	/* Driver can only run on flash code. ROM code results in errors so it
	 * should go into a suspended state if it can't initialize.
	 */
	if (!data->info.is_running_flash_code) {
		goto error;
	}

	/* Setup I2C1 interrupt mask for this port */
	rv = pdc_interrupt_mask_init(data);
	if (rv < 0) {
		LOG_ERR("TI%d: Write interrupt mask failed (%d)",
			cfg->connector_number, rv);
		goto error;
	}
	rv = pdc_autonegotiate_sink_reset(data);
	if (rv < 0) {
		LOG_ERR("TI%d: Reset autonegotiate_sink reg failed (%d)",
			cfg->connector_number, rv);
		goto error;
	}
	rv = pdc_port_control_init(data);
	if (rv < 0) {
		LOG_ERR("TI%d: Write port control failed (%d)",
			cfg->connector_number, rv);
		goto error;
	}
	rv = pdc_exit_dead_battery(data);
	if (rv < 0) {
		LOG_ERR("TI%d: Clear dead battery flag failed (%d)",
			cfg->connector_number, rv);
		goto error;
	}

	/* Set PDC notifications */
	data->cmd = CMD_SET_NOTIFICATION_ENABLE;
	/*
	 * Need to post PDC_CMD_EVENT so the command isn't cleared in
	 * st_idle_entry
	 */
	k_event_post(&data->pdc_event, PDC_CMD_EVENT);

	/* Transition to the idle state */
	set_state(data, ST_IDLE);
	return SMF_EVENT_HANDLED;

error:
	set_state_delayed_post(data, ST_ERROR_RECOVERY,
			       PDC_INIT_ERROR_RECOVERY_DELAY_MS);
	return SMF_EVENT_HANDLED;
}

static void st_idle_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);

	/* Reset the command if no pending PDC_CMD_EVENT */
	if (!k_event_test(&data->pdc_event, PDC_CMD_EVENT)) {
		data->cmd = CMD_NONE;
	}

	/* Reset running ucsi command back to invalid. */
	data->running_ucsi_cmd = 0;
}

static enum smf_state_result st_idle_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	uint32_t events = data->events;

	if (check_comms_suspended()) {
		/* Do not start executing commands or processing IRQs if
		 * suspended. We don't need to check the event flag, it is
		 * only needed to wake this thread.
		 */
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}
	if (events & PDC_CMD_COMPLETE_EVENT) {
		k_event_clear(&data->pdc_event, PDC_CMD_COMPLETE_EVENT);
		data->cci_event.command_completed = 1;
		call_cci_event_cb(data);

		/* Re-enter idle state. */
		set_state(data, ST_IDLE);
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
			task_ucsi(data, UCSI_SET_PDOS);
			break;
		case CMD_GET_CONNECTOR_STATUS:
		case CMD_GET_VBUS_VOLTAGE:
			task_ucsi(data, UCSI_GET_CONNECTOR_STATUS);
			break;
		case CMD_GET_ERROR_STATUS:
			task_ucsi(data, UCSI_GET_ERROR_STATUS);
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
		case CMD_SET_RDO:
			cmd_set_rdo(data);
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
		case CMD_SET_FRS:
			cmd_set_frs(data);
			break;
		case CMD_SET_DRP_MODE:
			cmd_set_drp_mode(data);
			break;
		case CMD_GET_DRP_MODE:
			cmd_get_drp_mode(data);
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
			break;
		case CMD_UPDATE_RETIMER:
			cmd_update_retimer(data);
			break;
		case CMD_RECONNECT:
			task_disc(data);
			break;
		case CMD_GET_CURRENT_PDO:
			cmd_get_current_pdo(data);
			break;
		case CMD_IS_VCONN_SOURCING:
			cmd_is_vconn_sourcing(data);
			break;
		case CMD_SET_SBU_MUX_MODE:
			task_sbud(data);
			break;
		case CMD_GET_SBU_MUX_MODE:
			cmd_get_sbu_mux_mode(data);
			break;
		case CMD_RAW_UCSI:
			task_raw_ucsi(data);
			break;
		case CMD_SET_DRS:
			cmd_set_drs(data);
			break;
		case CMD_SET_SX_APP_CONFIG:
			cmd_set_sx_app_config(data);
			break;
		case CMD_SET_BBR_CTS:
			task_trig(data);
			break;
		case CMD_GET_ATTENTION_VDO:
			cmd_get_attention_vdo(data);
		}
	}

	return SMF_EVENT_HANDLED;
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

static enum smf_state_result st_error_recovery_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	/* Don't continue trying if we are suspending communication */
	if (check_comms_suspended()) {
		set_state(data, ST_SUSPENDED);
		return SMF_EVENT_HANDLED;
	}

	/* TODO: Add proper error recovery */
	/* Currently this state is entered when an I2C command fails */

	/* Command has completed with an error */
	data->cci_event.command_completed = 1;
	data->cci_event.error = 1;

	/* Inform the system of the event */
	call_cci_event_cb(data);

	if (data->init_done) {
		/* Transition to idle */
		set_state(data, ST_IDLE);
	} else {
		set_state(data, ST_INIT);
	}
	return SMF_EVENT_HANDLED;
}

static void st_suspended_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static enum smf_state_result st_suspended_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	if (data->events & PDC_CMD_SUSPEND_REQUEST_EVENT) {
		k_event_clear(&data->pdc_event, PDC_CMD_SUSPEND_REQUEST_EVENT);
	}

	/* Stay here while suspended */
	if (check_comms_suspended()) {
		return SMF_EVENT_HANDLED;
	}

	data->init_attempt = 0;
	set_state(data, ST_INIT);
	return SMF_EVENT_HANDLED;
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
		LOG_ERR("TI%d: Read port configuration failed (%d)",
			cfg->connector_number, rv);
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
		LOG_ERR("TI%d: Unsupported DRP mode", cfg->connector_number);
		set_state(data, ST_IDLE);
		return;
	}

	/* Write PDC port configuration */
	rv = tps_rw_port_configuration(&cfg->i2c, &pdc_port_configuration,
				       I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write port configuration failed (%d)",
			cfg->connector_number, rv);
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

static void cmd_set_drs(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_control pdc_port_control;
	int rv;

	/* Read PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Read port control failed (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	/*
	 * Either uor.swap_to_dfp or uor.swap_to_ufp should be set. Having both
	 * set is not allowed per the UCSI spec. SET_UOR can't be sent directly
	 * as a UCSI command for two reasons.
	 *  1. If a hard reset occurs while the port is in a SNK power role then
	 *     there is no mechanism to trigger a data role swap to the desired
	 *     data role. Setting initiate_swap_to_dfp|ufp instructs the PDC to
	 *     automatically trigger a data role swap request to the desired
	 *     data role following the establishment of a new PD contract.
	 *
	 *  2. If uor.accept_dr_swap is set to 0, which will usually be the case
	 *     if the data role is DFP, then the TI PDC will clear the data role
	 *     capable bit in the SRC/SNK CAP which then causes issues with
	 *     complicance test TD 4.11.1
	 *
	 * So SET_UOR is instead mapped to the port control register which
	 * provides the required control for data role swaps while still
	 * allowing compliance tests to pass.
	 */
	if (data->uor.swap_to_dfp) {
		pdc_port_control.initiate_swap_to_dfp = 1;
		pdc_port_control.initiate_swap_to_ufp = 0;
	} else {
		pdc_port_control.initiate_swap_to_ufp = 1;
		pdc_port_control.initiate_swap_to_dfp = 0;
	}

	/* Always want to accept a request to swap to DFP */
	pdc_port_control.process_swap_to_dfp = 1;
	/* accept_dr_swap control applies to either DFP or UFP */
	pdc_port_control.process_swap_to_ufp = data->uor.accept_dr_swap;

	/* Write PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write port control failed (%d)",
			cfg->connector_number, rv);
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

static void cmd_get_drp_mode(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_configuration pdc_port_configuration;
	uint8_t *drp_mode = (uint8_t *)data->user_buf;
	int rv;

	/* Read PDC port configuration */
	rv = tps_rw_port_configuration(&cfg->i2c, &pdc_port_configuration,
				       I2C_MSG_READ);

	*drp_mode = pdc_port_configuration.typec_support_options;

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
		LOG_ERR("TI%d: Read port control failed (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	pdc_port_control.typec_current = data->tcc;

	/* Write PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write port control failed (%d)",
			cfg->connector_number, rv);
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

static void cmd_set_frs(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_control pdc_port_control;
	int rv;

	/* Read PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Read port control failed (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	LOG_INF("TI%d: SET FRS %d", cfg->connector_number,
		data->fast_role_swap);
	pdc_port_control.fr_swap_enabled = data->fast_role_swap;

	/* Write PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write port control failed (%d)",
			cfg->connector_number, rv);
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
		LOG_ERR("TI%d: Null buffer; can't read RDO",
			cfg->connector_number);
		goto error_recovery;
	}

	rv = tps_rd_active_rdo_contract(&cfg->i2c, &active_rdo_contract);
	if (rv) {
		LOG_ERR("TI%d: Failed to read active RDO (%d)",
			cfg->connector_number, rv);
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

static void cmd_get_current_pdo(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_active_pdo_contract active_pdo_contract;
	uint32_t *pdo = (uint32_t *)data->user_buf;
	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("TI%d: Null buffer; can't read PDO",
			cfg->connector_number);
		goto error_recovery;
	}

	rv = tps_rd_active_pdo_contract(&cfg->i2c, &active_pdo_contract);
	if (rv) {
		LOG_ERR("TI%d: Failed to read active PDO (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	*pdo = active_pdo_contract.active_pdo;

	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_update_retimer(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_port_control pdc_port_control;
	int rv;

	/* Read PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Read port control failed (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	pdc_port_control.retimer_fw_update = data->retimer_feature_en;

	/* Write PDC port control */
	rv = tps_rw_port_control(&cfg->i2c, &pdc_port_control, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write port control failed (%d)",
			cfg->connector_number, rv);
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

static void cmd_is_vconn_sourcing(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_power_path_status pdc_power_path_status;
	int rv;
	bool *is_vconn_sourcing = (bool *)data->user_buf;
	uint32_t ext_vconn_sw;

	rv = tps_rd_power_path_status(&cfg->i2c, &pdc_power_path_status);
	if (rv) {
		LOG_ERR("TI%d: Failed to power path status (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}
	ext_vconn_sw = is_first_port_on_chip(data) ?
			       pdc_power_path_status.pa_vconn_sw :
			       pdc_power_path_status.pb_vconn_sw;

	*is_vconn_sourcing = (ext_vconn_sw & 0x2) != 0;

	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	/* Transition to idle */
	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_set_rdo(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_autonegotiate_sink an_snk;
	int rv, an_max_a, an_max_v, an_min_v, an_min_power, an_cap_mismatch;
	uint32_t pdo = data->cached_pdos[RDO_POS(data->rdo) - 1];

	if (!atomic_get(&data->set_rdo_possible)) {
		k_work_reschedule(&data->new_power_contract,
				  K_MSEC(PDC_TI_SET_SINK_PATH_DELAY_MS));
		/* Save CMD for callback function */
		data->delayable_cmd = data->cmd;
		return;
	}

	rv = tps_rw_autonegotiate_sink(&cfg->i2c, &an_snk, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Failed to read auto negotiate sink register (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	an_cap_mismatch = CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW / 250;
	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		an_max_v = PDO_BATT_MAX_VOLTAGE(pdo) / 50;
		an_min_v = PDO_BATT_MIN_VOLTAGE(pdo) / 50;
		an_max_a = CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA / 10;
		an_min_power = PDO_BATT_MAX_POWER(pdo) / 1000 / 250;
	} else {
		an_max_v = an_min_v = PDO_FIXED_VOLTAGE(pdo) / 50;
		an_max_a = min(CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
			       PDO_FIXED_CURRENT(pdo)) /
			   10;
		an_min_power = (an_max_v * an_max_a) / 500;
	}

	/* If the antonegotiation sink register isn't meaningfully updated,
	 * exit here. The PDC will have already sent the expected RDO.
	 */
	if (an_snk.auto_compute_sink_min_power == 0 &&
	    an_snk.auto_compute_sink_min_voltage == 0 &&
	    an_snk.auto_compute_sink_max_voltage == 0 &&
	    an_snk.auto_neg_max_current == an_max_a &&
	    an_snk.auto_neg_sink_min_required_power == an_min_power &&
	    an_snk.auto_neg_max_voltage == an_max_v &&
	    an_snk.auto_neg_min_voltage == an_min_v &&
	    an_snk.auto_neg_capabilities_mismach_power == an_cap_mismatch) {
		set_state(data, ST_TASK_WAIT);
		return;
	}

	an_snk.auto_compute_sink_min_power = 0;
	an_snk.auto_compute_sink_min_voltage = 0;
	an_snk.auto_compute_sink_max_voltage = 0;
	an_snk.auto_neg_max_current = an_max_a;
	an_snk.auto_neg_sink_min_required_power = an_min_power;
	an_snk.auto_neg_max_voltage = an_max_v;
	an_snk.auto_neg_min_voltage = an_min_v;
	an_snk.auto_neg_capabilities_mismach_power = an_cap_mismatch;

	rv = tps_rw_autonegotiate_sink(&cfg->i2c, &an_snk, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Failed to write auto negotiate sink register (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	task_aneg(data);
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
			LOG_ERR("TI%d: Failed to read partner identity ACK (%d)",
				cfg->connector_number, rv);
			goto error_recovery;
		}
	} else if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP_PRIME) {
		rv = tps_rd_received_sop_prime_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("TI%d: Failed to read cable identity ACK (%d)",
				cfg->connector_number, rv);
			goto error_recovery;
		}
	} else {
		/* Unsupported */
		LOG_ERR("TI%d: Unsupported VDO origin", cfg->connector_number);
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
			LOG_ERR("TI%d: Failed to read partner VDO (%d)",
				cfg->connector_number, rv);
			goto error_recovery;
		}
	} else if (data->vdo_req.vdo_origin == VDO_ORIGIN_SOP_PRIME) {
		rv = tps_rd_received_sop_prime_identity_data_object(
			&cfg->i2c, &received_identity_data_object);
		if (rv) {
			LOG_ERR("TI%d: Failed to read cable VDO (%d)",
				cfg->connector_number, rv);
			goto error_recovery;
		}
	} else {
		/* Unsupported */
		LOG_ERR("TI%d: Unsupported VDO origin", cfg->connector_number);
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
 * @param cfg Pointer to the device's config struct
 * @param info Output param for chip info
 * @return 0 on success or an error code
 */
static int cmd_get_ic_status_sync_internal(const struct pdc_config_t *cfg,
					   struct pdc_info_t *info)
{
	union reg_version version;
	union reg_tx_identity tx_identity;
	union reg_customer_use customer_val;
	union reg_mode mode_reg;
	union reg_boot_flags pdc_boot_flags;
	int rv;

	if (info == NULL) {
		return -EINVAL;
	}

	rv = tps_rd_version(&cfg->i2c, &version);
	if (rv) {
		LOG_ERR("TI%d: Failed to read version (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	rv = tps_rw_customer_use(&cfg->i2c, &customer_val, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Failed to read customer register (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	rv = tps_rw_tx_identity(&cfg->i2c, &tx_identity, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Failed to read Tx identity (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	rv = tps_rd_mode(&cfg->i2c, &mode_reg);
	if (rv) {
		LOG_ERR("TI%d: Failed to read mode (%d)", cfg->connector_number,
			rv);
		return rv;
	}

	uint32_t mode = *(uint32_t *)mode_reg.data;

	info->is_running_flash_code =
		(mode == REG_MODE_APP0 || mode == REG_MODE_APP1);

	/* TI FW main version */
	info->fw_version = version.version;

	/* TI VID (little-endian) */
	info->vid = *(uint16_t *)tx_identity.vendor_id;

	/* TI PID (little-endian) */
	info->pid = *(uint16_t *)tx_identity.product_id;

	/* TI Running flash bank offset */
	rv = tps_rd_boot_flags(&cfg->i2c, &pdc_boot_flags);
	if (rv) {
		LOG_ERR("TI%d: Read boot flags failed (%d)",
			cfg->connector_number, rv);
		return rv;
	}
	info->running_in_flash_bank = pdc_boot_flags.active_bank;

	/* TI PD Revision (big-endian) */
	info->pd_revision = 0x0000;

	/* TI PD Version (big-endian) */
	info->pd_version = 0x0000;

	memset(info->project_name, 0, sizeof(info->project_name));
	if (memcmp(customer_val.data, "GOOG", strlen("GOOG")) == 0) {
		/* Using the unified config identifier scheme */
		memcpy(info->project_name, customer_val.data,
		       sizeof(customer_val.data));
	} else {
		/* Old scheme of incrementing an integer in the customer use
		 * reg. Convert to an ASCII string.
		 */
		snprintf(info->project_name, sizeof(info->project_name), "TI%d",
			 customer_val.data[0]);
	}

	LOG_HEXDUMP_DBG(customer_val.data, sizeof(customer_val.data),
			"Customer use raw value:");

	/* Fill in the chip type (driver compat string) */
	strncpy(info->driver_name, STRINGIFY(DT_DRV_COMPAT),
		sizeof(info->driver_name));
	info->driver_name[sizeof(info->driver_name) - 1] = '\0';

	info->no_fw_update = cfg->no_fw_update;

	return 0;
}

static void cmd_get_ic_status(struct pdc_data_t *data)
{
	struct pdc_info_t *info = (struct pdc_info_t *)data->user_buf;
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = cmd_get_ic_status_sync_internal(cfg, info);
	if (rv) {
		LOG_ERR("TI%d: Could not get chip info (%d)",
			cfg->connector_number, rv);
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

static void cmd_get_pdc_data_status_reg(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data_status data_status;

	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("TI%d: Null user buffer; can't read data status reg",
			cfg->connector_number);
		goto error_recovery;
	}

	rv = tps_rd_data_status_reg(&cfg->i2c, &data_status);
	if (rv) {
		LOG_ERR("TI%d: Failed to read data status reg (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	memcpy(data->user_buf, data_status.raw_value, sizeof(data_status));

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_get_sbu_mux_mode(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_status status;
	enum pdc_sbu_mux_mode *mode = (enum pdc_sbu_mux_mode *)data->user_buf;

	int rv;

	rv = tps_rd_status_reg(&cfg->i2c, &status);
	if (rv) {
		LOG_ERR("TI%d: Failed to read status reg (%d)",
			cfg->connector_number, rv);
		*mode = PDC_SBU_MUX_MODE_INVALID;
		goto error_recovery;
	}

	*mode = status.sbumux_mode;

	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	set_state(data, ST_IDLE);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void cmd_set_sx_app_config(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_sx_app_config pdc_sx_app_config;
	int rv;

	/* Read PDC sx app config */
	rv = tps_rw_sx_app_config(&cfg->i2c, &pdc_sx_app_config, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("TI%d: Read sx app config failed (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	/* This register only has one non-reserved field */
	pdc_sx_app_config.sleep_state = data->sx_state;

	/* Write PDC sx app config */
	rv = tps_rw_sx_app_config(&cfg->i2c, &pdc_sx_app_config, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("TI%d: Write sx app config failed (%d)",
			cfg->connector_number, rv);
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

static void cmd_get_attention_vdo(struct pdc_data_t *data)
{
	union reg_received_attention_vdm received_attention_vdm;
	struct pdc_config_t const *cfg = data->dev->config;

	int rv;

	if (data->user_buf == NULL) {
		LOG_ERR("TI%d: Null user buffer; can't read attention reg",
			cfg->connector_number);
		goto error_recovery;
	}

	rv = tps_rd_received_attention_vdm(&cfg->i2c, &received_attention_vdm);
	if (rv) {
		LOG_ERR("TI%d: Failed to read received attention vdm (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	union get_attention_vdo_t get_attention_vdo = {
		.alt_mode_index = 0,
		.num_vdos = received_attention_vdm.number_valid_vdos,
		.sequence_number = received_attention_vdm.sequence_number,
		.vdm_heade = received_attention_vdm.vdm_header,
		.vdo = received_attention_vdm.vdo,
	};
	memcpy(data->user_buf, &get_attention_vdo,
	       sizeof(union get_attention_vdo_t));

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

static void task_trig(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_thunderbolt_configuration pdc_thunderbolt_config;
	union reg_data cmd_data;
	int rv;

	rv = tps_rw_thunderbolt_configuration(
		&cfg->i2c, &pdc_thunderbolt_config, I2C_MSG_READ);
	if (rv) {
		LOG_ERR("Read thunderbolt config failed");
		goto error_recovery;
	}

	pdc_thunderbolt_config.retimer_compliance_support =
		data->retimer_feature_en;

	rv = tps_rw_thunderbolt_configuration(
		&cfg->i2c, &pdc_thunderbolt_config, I2C_MSG_WRITE);
	if (rv) {
		LOG_ERR("Write thunderbolt config failed");
		goto error_recovery;
	}

	cmd_data.data[0] = data->retimer_feature_en ? RISING_EDGE :
						      FALLING_EDGE;
	cmd_data.data[1] = EVENT_RETIMER_SOC_OVR_FORCE_PWR;

	rv = write_task_cmd(cfg, COMMAND_TASK_TRIG, &cmd_data);
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

static void task_gaid(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = write_task_cmd(cfg, COMMAND_TASK_GAID, NULL);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	/* After triggering a reset (TASK_COMMAND_GAID), it takes >1s to
	 * recover. Send all ports back to INIT after doing this.
	 */
	set_all_ports_to_init(PDC_TI_GAID_DELAY_MS);
	return;
}

static void task_srdy(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	union reg_power_path_status pdc_power_path_status;
	int rv;
	uint32_t ext_vbus_sw;
	bool cur_sink_enabled;

	rv = tps_rd_power_path_status(&cfg->i2c, &pdc_power_path_status);
	if (rv) {
		LOG_ERR("TI%d: Failed to power path status (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	ext_vbus_sw = (is_first_port_on_chip(data) ?
			       pdc_power_path_status.pa_ext_vbus_sw :
			       pdc_power_path_status.pb_ext_vbus_sw);
	cur_sink_enabled = (ext_vbus_sw == EXT_VBUS_SWITCH_ENABLED_INPUT);

	if (data->snk_fet_en && !cur_sink_enabled) {
		if (!atomic_get(&data->sink_enable_possible)) {
			/* Retry this command within timeout if a new power
			 * contract is seen. Otherwise, it will return an error
			 * to the caller.
			 */
			/* Save CMD for callback function */
			data->delayable_cmd = data->cmd;
			k_work_reschedule(
				&data->new_power_contract,
				K_MSEC(PDC_TI_SET_SINK_PATH_DELAY_MS));
			return;
		}

		/* TODO(b/358274846) - Check whether this can be moved to
		 * appconfig so we don't have to select by port index on chip.
		 */
		cmd_data.data[0] = is_first_port_on_chip(data) ?
					   SWITCH_SELECT_PP_EXT2 :
					   SWITCH_SELECT_PP_EXT1;
		/* Enable Sink FET */
		rv = write_task_cmd(cfg, COMMAND_TASK_SRDY, &cmd_data);
	} else if (!data->snk_fet_en && cur_sink_enabled) {
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
		LOG_ERR("TI%d: Failed to write command (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	/* Transition to wait state */
	set_state(data, ST_TASK_WAIT);
	return;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
}

static void task_dbfg(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = write_task_cmd(cfg, COMMAND_TASK_DBFG, NULL);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_aneg(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	rv = write_task_cmd(cfg, COMMAND_TASK_ANEG, NULL);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_sbud(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	int rv;

	cmd_data.data[0] = data->sbumux_mode;
	rv = write_task_cmd(cfg, COMMAND_TASK_SBUD, &cmd_data);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_disc(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	int rv;

	/* Disconnect for 3 seconds then reconnect, adjustable. */
	cmd_data.data[0] = 3;
	rv = write_task_cmd(cfg, COMMAND_TASK_DISC, &cmd_data);
	if (rv) {
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_ucsi(struct pdc_data_t *data, enum ucsi_command_t ucsi_command)
{
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_data cmd_data;
	union ucsi_set_pdos_t *ucsi_pdos;
	int rv;

	/* Set the currently running UCSI command. */
	data->running_ucsi_cmd = ucsi_command;

	memset(cmd_data.data, 0, sizeof(cmd_data.data));
	/* Byte 0: UCSI Command Code */
	cmd_data.data[0] = ucsi_command;
	/* Byte 1: Data length per UCSI spec */
	cmd_data.data[1] = 0;
	/* Connector Number: Byte 2, bits 6:0. Bit 7 is reserved */
	cmd_data.data[2] = cfg->port_index_on_chip;

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
		cmd_data.data[2] |= (data->pdo_source << 7);
		/* PDO Offset: Byte 3, bits 7:0 */
		cmd_data.data[3] = data->pdo_offset;
		/* Number of PDOs: Byte 4, bits 1:0 */
		cmd_data.data[4] = data->num_pdos - 1;
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
	case CMD_SET_PDOS:
		/* ucsi_set_pdos starts with connector number */
		ucsi_pdos = (union ucsi_set_pdos_t *)&cmd_data.data[2];
		/* SRC or SNK PDO */
		ucsi_pdos->pdo_type = data->pdo_type;
		/* Number of PDOs being set */
		ucsi_pdos->number_of_pdos = data->num_pdos;
		/* No chunking, so index is always 0 */
		ucsi_pdos->data_index = 0;
		/* No chunking, so always end of message */
		ucsi_pdos->end_of_message = 1;
		/* PDOs to send start at cmd_data[8] */
		memcpy(&cmd_data.data[8], data->pdos,
		       data->num_pdos * sizeof(uint32_t));
		/* Update Data Length to reflect number of PDOs */
		cmd_data.data[1] = data->num_pdos * sizeof(uint32_t);
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
		LOG_ERR("TI%d: Failed to write UCSI command 0x%02x (%d)",
			cfg->connector_number, ucsi_command, rv);
		set_state(data, ST_ERROR_RECOVERY);
		return;
	}

	/* Transition to wait state */
	set_state(data, ST_TASK_WAIT);
	return;
}

static void task_raw_ucsi(struct pdc_data_t *data)
{
	struct pdc_config_t const *cfg = data->dev->config;
	int rv;

	/* Byte 0 of |union reg_data.data| is the ucsi command. */
	data->running_ucsi_cmd = data->raw_ucsi_cmd_data.data[0];

	rv = write_task_cmd(cfg, COMMAND_TASK_UCSI, &data->raw_ucsi_cmd_data);

	/* Transition to wait state */
	set_state(data, ST_TASK_WAIT);
	return;
}

static void st_task_wait_entry(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;

	print_current_state(data);
}

static void tps_check_data_ready(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pdc_data_t *data =
		CONTAINER_OF(dwork, struct pdc_data_t, data_ready);

	k_event_post(&data->pdc_event, PDC_INTERNAL_EVENT);
}

static enum smf_state_result st_task_wait_run(void *o)
{
	struct pdc_data_t *data = (struct pdc_data_t *)o;
	struct pdc_config_t const *cfg = data->dev->config;
	union reg_command cmd;
	union reg_data cmd_data;
	uint8_t offset;
	uint32_t len = 0;
	int rv;

	/* Read command register for the particular port */
	rv = tps_rw_command_for_i2c1(&cfg->i2c, &cmd, I2C_MSG_READ);
	if (rv) {
		/* I2C transaction failed */
		LOG_ERR("TI%d: Failed to read command (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	/*
	 * Wait for command to complete:
	 *  1) command is set to 0 when command is sent
	 *  2) command is set to "!CMD" for unknown command
	 */
	if (cmd.command && cmd.command != COMMAND_TASK_NO_COMMAND) {
		LOG_INF("TI%d: Data not ready, check again in %d ms",
			cfg->connector_number, PDC_TI_DATA_READY_TIME_MS);
		k_work_reschedule(&data->data_ready,
				  K_MSEC(PDC_TI_DATA_READY_TIME_MS));
		return SMF_EVENT_HANDLED;
	}

	/*
	 * Read status of command for particular port:
	 *  1) cmd_data is set to zero on success
	 *  2) cmd_data is set to an error code on failure
	 */
	rv = tps_rw_data_for_cmd1(&cfg->i2c, &cmd_data, I2C_MSG_READ);
	if (rv) {
		/* I2C transaction failed */
		LOG_ERR("TI%d: Failed to read command (%d)",
			cfg->connector_number, rv);
		goto error_recovery;
	}

	/* Data byte offset 0 is the return error code */
	if (cmd.command || cmd_data.data[0] != 0) {
		/* Command has completed with error */
		if (cmd.command == COMMAND_TASK_NO_COMMAND) {
			LOG_DBG("TI%d: Command %d not supported",
				cfg->connector_number, data->cmd);
		} else {
			LOG_DBG("TI%d: Command %d failed. Err : %d",
				cfg->connector_number, data->cmd,
				cmd_data.data[0]);
		}
		data->cci_event.error = 1;
		goto data_out;
	}

	switch (data->cmd) {
	case CMD_SET_NOTIFICATION_ENABLE:
		/* Initialization for driver is done once notifications are
		 * enabled. This flag is reset when the INIT state is entered.
		 */
		data->init_done = true;
		k_event_post(&data->pdc_event, PDC_IRQ_EVENT);
		break;
	case CMD_SET_RDO:
		/* Re-set sink enable until after aNEG completes. */
		atomic_set(&data->sink_enable_possible, 0);
		break;
	default:
		break;
	}

	switch (data->running_ucsi_cmd) {
	case UCSI_GET_CAPABILITY:
		offset = 1;
		struct capability_t *cp =
			(struct capability_t *)&cmd_data.data[offset];
		/*
		 * TODO(b/414863461) get_pd_message is not being set by the PDC,
		 * but this is required for the kernel UCSI driver to trigger it
		 * sending UCSI_GET_PD_MESSAGE for populating discovery
		 * information.
		 */
		cp->bmOptionalFeatures.get_pd_message = 1;
		len = sizeof(struct capability_t);
		break;
	case UCSI_GET_CONNECTOR_CAPABILITY:
		offset = 1;
		len = sizeof(union connector_capability_t);
		break;
	case UCSI_GET_CONNECTOR_STATUS: {
		offset = 1;
		union connector_status_t *cs =
			(union connector_status_t *)&cmd_data.data[offset];
		if (data->cmd == CMD_GET_VBUS_VOLTAGE) {
			uint16_t *voltage = (uint16_t *)data->user_buf;
			len = 0;
			*voltage = cs->voltage_reading * cs->voltage_scale * 5;
		} else {
			len = sizeof(union connector_status_t);
			/* If we had previously cached the connection status
			 * change, append those bits in GET_CONNECTOR_STATUS.
			 * The PDC clears these after the first read but we want
			 * these to be visible until they are ACK-ed.
			 */
			if (data->use_cached_conn_status_change) {
				cs->raw_conn_status_change_bits |=
					data->cached_conn_status
						.raw_conn_status_change_bits;
			}

			/* Cache result of GET_CONNECTOR_STATUS and use this for
			 * subsequent calls.
			 */
			data->cached_conn_status = *cs;
			data->use_cached_conn_status_change = true;
			if (!cs->connect_status)
				pdc_autonegotiate_sink_reset(data);
		}
		break;
	}
	case UCSI_GET_CABLE_PROPERTY:
		offset = 1;
		len = sizeof(union cable_property_t);
		break;
	case UCSI_GET_CURRENT_CAM:
		offset = 1;
		len = sizeof(uint32_t);
		break;
	case UCSI_GET_ALTERNATE_MODES:
	case UCSI_GET_ERROR_STATUS:
		offset = 2;
		len = cmd_data.data[1];
		break;
	case UCSI_GET_PDOS: {
		len = cmd_data.data[1];
		offset = 2;
		if (data->cmd == CMD_GET_PDOS) {
			memcpy(data->cached_pdos + data->pdo_offset,
			       &cmd_data.data[offset], len);
		}
		break;
	}
	case UCSI_GET_PD_MESSAGE:
		offset = 2;
		union get_pd_message_t get_pd_message_cmd;
		memcpy(&get_pd_message_cmd,
		       &data->raw_ucsi_cmd_data.data[offset],
		       sizeof(union get_pd_message_t));
		switch (get_pd_message_cmd.response_message_type) {
		case GET_PD_MESSAGE_DISC_ID:
			len = sizeof(uint32_t) * PDC_DISC_IDENTITY_VDO_COUNT;
			break;
		case GET_PD_MESSAGE_REVISION:
			len = sizeof(uint32_t);
			break;
		default:
			/* Unsupported GET_PD_MESSAGE command */
			offset = 0;
			len = 0;
		}
		break;
	default:
		/* No data for this command */
		len = 0;
	}

data_out:
	if (data->user_buf && len) {
		if (data->cci_event.error) {
			memset(data->user_buf, 0, len);
		} else {
			/* No preprocessing needed for the user data */
			memcpy(data->user_buf, &cmd_data.data[offset], len);
		}
	}

	/* Set cci.data_len. This will be zero if no data is available. */
	data->cci_event.data_len = len;
	/* Command has completed */
	data->cci_event.command_completed = 1;
	/* Inform the system of the event */
	call_cci_event_cb(data);

	if (data->init_done) {
		/* Transition to idle state */
		set_state(data, ST_IDLE);
	} else {
		/* Re-try init since we didn't complete successfully. */
		set_state(data, ST_INIT);
	}
	return SMF_EVENT_HANDLED;

error_recovery:
	set_state(data, ST_ERROR_RECOVERY);
	return SMF_EVENT_HANDLED;
}

/* Populate state table */
static const struct smf_state states[] = {
	[ST_INIT] =
		SMF_CREATE_STATE(st_init_entry, st_init_run, NULL, NULL, NULL),
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

static int tps_post_command_with_callback(const struct device *dev,
					  enum cmd_t cmd,
					  union reg_data *cmd_data,
					  void *user_buf,
					  struct pdc_callback *callback)
{
	struct pdc_data_t *data = dev->data;

	/* TODO(b/345783692): Double check this logic. */
	if (get_state(data) != ST_IDLE) {
		return -EBUSY;
	}

	/* Raw UCSI calls must provide the cmd data to be sent. */
	if (cmd == CMD_RAW_UCSI && cmd_data == NULL) {
		return -EINVAL;
	}

	if (k_mutex_lock(&data->mtx, K_MSEC(100)) == 0) {
		if (data->cmd != CMD_NONE) {
			k_mutex_unlock(&data->mtx);
			return -EBUSY;
		}

		data->user_buf = user_buf;
		data->cmd = cmd;
		data->cc_cb_tmp = callback;
		if (cmd_data) {
			memcpy(&data->raw_ucsi_cmd_data, cmd_data,
			       sizeof(*cmd_data));
		}

		k_mutex_unlock(&data->mtx);
		k_event_post(&data->pdc_event, PDC_CMD_EVENT);
	} else {
		return -EBUSY;
	}

	return 0;
}

static int tps_post_command(const struct device *dev, enum cmd_t cmd,
			    void *user_buf)
{
	return tps_post_command_with_callback(dev, cmd, NULL, user_buf, NULL);
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

	/* Clear cached status bits with given mask. */
	if (ci.raw_value) {
		data->cached_conn_status.raw_conn_status_change_bits &=
			~(ci.raw_value);
	}

	k_event_post(&data->pdc_event, PDC_CMD_COMPLETE_EVENT);

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
	struct pdc_data_t *data = dev->data;
	if (!data->cached_conn_status.power_direction) {
		return -ENOSYS;
	}

	return tps_post_command(dev, CMD_READ_POWER_LEVEL, NULL);
}

static int tps_reconnect(const struct device *dev)
{
	return tps_post_command(dev, CMD_RECONNECT, NULL);
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
	const struct pdc_config_t *cfg = data->dev->config;

	/* Sanitize and convert input */
	switch (tcc) {
	case TC_CURRENT_3_0A:
		data->tcc = TI_3_0_A;
		break;
	case TC_CURRENT_1_5A:
		data->tcc = TI_1_5_A;
		break;
	case TC_CURRENT_USB_DEFAULT:
		data->tcc = TI_TYPEC_DEFAULT;
		break;
	case TC_CURRENT_PPM_DEFINED:
	default:
		LOG_ERR("TI%d: Unsupported Type-C current: %u",
			cfg->connector_number, tcc);
		return -EINVAL;
	}

	return tps_post_command(dev, CMD_SET_TPC_RP, NULL);
}

static int tps_set_fast_role_swap(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	data->fast_role_swap = enable;

	return tps_post_command(dev, CMD_SET_FRS, NULL);
}

static int tps_set_sbu_mux_mode(const struct device *dev,
				enum pdc_sbu_mux_mode mode)
{
	struct pdc_data_t *data = dev->data;

	data->sbumux_mode = mode;

	return tps_post_command(dev, CMD_SET_SBU_MUX_MODE, NULL);
}

static int tps_get_sbu_mux_mode(const struct device *dev,
				enum pdc_sbu_mux_mode *mode)
{
	if (mode == NULL)
		return -EINVAL;

	return tps_post_command(dev, CMD_GET_SBU_MUX_MODE, mode);
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
	if (es == NULL) {
		return -EINVAL;
	}
	return tps_post_command(dev, CMD_GET_ERROR_STATUS, es);
}

static int tps_set_rdo(const struct device *dev, uint32_t rdo)
{
	struct pdc_data_t *data = dev->data;

	data->rdo = rdo;
	return tps_post_command(dev, CMD_SET_RDO, NULL);
}

static int tps_get_rdo(const struct device *dev, uint32_t *rdo)
{
	return tps_post_command(dev, CMD_GET_RDO, rdo);
}

static int tps_get_pdos(const struct device *dev, enum pdo_type_t pdo_type,
			enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			enum pdo_source_t source, uint32_t *pdos)
{
	struct pdc_data_t *data = dev->data;

	/* TODO(b/345783692): Make sure these accesses don't need to be
	 * synchronized.
	 */

	if (pdos == NULL) {
		return -EINVAL;
	}

	/* Note: num_pdos is range-checked by pdc_get_pdos() before
	 * calling into this driver implementation. */

	data->pdo_type = pdo_type;
	data->pdo_offset = pdo_offset;
	data->num_pdos = num_pdos;
	data->pdo_source = source;

	return tps_post_command(dev, CMD_GET_PDOS, pdos);
}

static int tps_set_pdos(const struct device *dev, enum pdo_type_t type,
			uint32_t *pdo, int count)
{
	struct pdc_data_t *data = dev->data;

	if (pdo == NULL) {
		return -EINVAL;
	}

	if (count <= 0 || count > PDO_MAX_OBJECTS) {
		return -ERANGE;
	}

	data->pdo_type = type;
	data->num_pdos = count;
	memcpy(data->pdos, pdo, sizeof(uint32_t) * count);

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

		LOG_DBG("TI%d: Use cached chip info (%u.%u.%u)",
			cfg->connector_number,
			PDC_FWVER_GET_MAJOR(data->info.fw_version),
			PDC_FWVER_GET_MINOR(data->info.fw_version),
			PDC_FWVER_GET_PATCH(data->info.fw_version));
		return 0;
	}

	/* Perform a live read (async) */
	return tps_post_command(dev, CMD_GET_IC_STATUS, info);
}

static int tps_get_hw_config(const struct device *dev,
			     struct pdc_hw_config_t *config)
{
	const struct pdc_config_t *cfg =
		(const struct pdc_config_t *)dev->config;

	if (config == NULL) {
		return -EINVAL;
	}

	config->bus_type = PDC_BUS_TYPE_I2C;
	config->i2c = cfg->i2c;
	config->ccd = cfg->ccd;

	return 0;
}

static int tps_get_vbus_voltage(const struct device *dev, uint16_t *voltage)
{
	if (voltage == NULL) {
		return -EINVAL;
	}
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

	return tps_post_command(dev, CMD_SET_DRS, NULL);
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

static int tps_get_drp_mode(const struct device *dev, enum drp_mode_t *dm)
{
	return tps_post_command(dev, CMD_GET_DRP_MODE, dm);
}

static int tps_update_retimer_mode(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	data->retimer_feature_en = enable;

	return tps_post_command(dev, CMD_UPDATE_RETIMER, NULL);
}

static int tps_get_current_pdo(const struct device *dev, uint32_t *pdo)
{
	return tps_post_command(dev, CMD_GET_CURRENT_PDO, pdo);
}

static int tps_is_vconn_sourcing(const struct device *dev, bool *vconn_sourcing)
{
	return tps_post_command(dev, CMD_IS_VCONN_SOURCING, vconn_sourcing);
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
		k_event_post(&data->pdc_event, PDC_IRQ_EVENT);

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

static void tps_start_thread(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;

	k_thread_start(data->thread);
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

static int tps_set_ap_power_state(const struct device *dev,
				  enum power_state state)
{
	struct pdc_data_t *data = dev->data;

	if (state == POWER_S0) {
		data->sx_state = SX_S0;
	} else if (state == POWER_S5) {
		data->sx_state = SX_S5;
	} else {
		return -EINVAL;
	}

	return tps_post_command(dev, CMD_SET_SX_APP_CONFIG, NULL);
}

static int tps_set_bbr_cts(const struct device *dev, bool enable)
{
	struct pdc_data_t *data = dev->data;

	data->retimer_feature_en = enable;

	return tps_post_command(dev, CMD_SET_BBR_CTS, NULL);
}

static int tps_get_attention_vdo(const struct device *dev,
				 union get_attention_vdo_t *vdo)
{
	return tps_post_command(dev, CMD_GET_ATTENTION_VDO, vdo);
}

static int tps_execute_ucsi_cmd(const struct device *dev, uint8_t ucsi_command,
				uint8_t data_size, uint8_t *command_specific,
				uint8_t *lpm_data_out,
				struct pdc_callback *callback)
{
	struct pdc_config_t const *cfg = dev->config;
	union reg_data cmd_data;
	enum cmd_t cmd = CMD_RAW_UCSI;
	int port_index_on_chip_byte_index;

	memset(cmd_data.data, 0, sizeof(cmd_data.data));
	/* Byte 0: UCSI Command Code */
	cmd_data.data[0] = ucsi_command;
	/* Byte 1: Data length per UCSI spec.
	 * TODO(b/360881314) - PPM should be forwarding this to driver
	 */
	cmd_data.data[1] = 0;

	/* If additional command specific bytes are provided, copy them. */
	if (data_size && command_specific) {
		memcpy(&cmd_data.data[2], command_specific, data_size);
	}

	/* TI UCSI tasks always require a port index on chip even when the UCSI
	 * spec doesn't require it. Except GET_ALTERNATE_MODES, all other
	 * commands will fit the port index on chip on Byte 2, bits 6:0. For
	 * GET_ALTERNATE_MODES, it is required and should be on Byte 3, bits
	 * 14:8.
	 */

	port_index_on_chip_byte_index =
		ucsi_command == UCSI_GET_ALTERNATE_MODES ? 3 : 2;
	cmd_data.data[port_index_on_chip_byte_index] &= ~0x7f;
	cmd_data.data[port_index_on_chip_byte_index] |=
		cfg->port_index_on_chip & 0x7f;

	return tps_post_command_with_callback(dev, cmd, &cmd_data, lpm_data_out,
					      callback);
}

static DEVICE_API(pdc, pdc_driver_api) = {
	.start_thread = tps_start_thread,
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
	.get_drp_mode = tps_get_drp_mode,
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
	.get_hw_config = tps_get_hw_config,
	.set_power_level = tps_set_power_level,
	.reconnect = tps_reconnect,
	.get_cable_property = tps_get_cable_property,
	.get_vdo = tps_get_vdo,
	.get_identity_discovery = tps_get_identity_discovery,
	.manage_callback = tps_manage_callback,
	.ack_cc_ci = tps_ack_cc_ci,
	.set_comms_state = tps_set_comms_state,
	.get_pch_data_status = tps_get_pch_data_status,
	.is_vconn_sourcing = tps_is_vconn_sourcing,
	.update_retimer = tps_update_retimer_mode,
	.execute_ucsi_cmd = tps_execute_ucsi_cmd,
	.set_frs = tps_set_fast_role_swap,
	.set_sbu_mux_mode = tps_set_sbu_mux_mode,
	.get_sbu_mux_mode = tps_get_sbu_mux_mode,
	.set_ap_power_state = tps_set_ap_power_state,
	.set_bbr_cts = tps_set_bbr_cts,
	.get_attention_vdo = tps_get_attention_vdo,
};

static void pdc_interrupt_callback(const struct device *dev,
				   struct gpio_callback *cb, uint32_t pins)
{
	struct pdc_data_t *data = CONTAINER_OF(cb, struct pdc_data_t, gpio_cb);

	k_event_post(&data->pdc_event, PDC_IRQ_EVENT);
}

static int pdc_init(const struct device *dev)
{
	const struct pdc_config_t *cfg = dev->config;
	struct pdc_data_t *data = dev->data;
	int rv;

	rv = i2c_is_ready_dt(&cfg->i2c);
	if (rv < 0) {
		LOG_ERR("TI%d: device %s not ready", cfg->connector_number,
			cfg->i2c.bus->name);
		return -ENODEV;
	}

	rv = gpio_is_ready_dt(&cfg->irq_gpios);
	if (rv < 0) {
		LOG_ERR("TI%d: device %s not ready", cfg->connector_number,
			cfg->irq_gpios.port->name);
		return -ENODEV;
	}

	k_event_init(&data->pdc_event);
	k_mutex_init(&data->mtx);
	k_work_init_delayable(&data->data_ready, tps_check_data_ready);
	k_work_init_delayable(&data->new_power_contract,
			      tps_notify_new_power_contract);
	k_work_init_delayable(&data->delayed_post, tps_delayed_post);

	data->cmd = CMD_NONE;
	data->init_done = false;
	data->info.fw_version = PDC_FWVER_INVALID;

	rv = gpio_pin_configure_dt(&cfg->irq_gpios, GPIO_INPUT);
	if (rv < 0) {
		LOG_ERR("TI%d: Unable to configure GPIO (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	gpio_init_callback(&data->gpio_cb, pdc_interrupt_callback,
			   BIT(cfg->irq_gpios.pin));

	rv = gpio_add_callback(cfg->irq_gpios.port, &data->gpio_cb);
	if (rv < 0) {
		LOG_ERR("TI%d: Unable to add callback (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	rv = gpio_pin_interrupt_configure_dt(&cfg->irq_gpios,
					     GPIO_INT_EDGE_FALLING);
	if (rv < 0) {
		LOG_ERR("TI%d: Unable to configure interrupt (%d)",
			cfg->connector_number, rv);
		return rv;
	}

	/* Set initial state */
	smf_set_initial(SMF_CTX(data), &states[ST_INIT]);

	/* Create the thread for this port */
	cfg->create_thread(dev);

	/* Trigger an interrupt on startup */
	k_event_post(&data->pdc_event, PDC_IRQ_EVENT);

	LOG_INF("TI%d: TI TPS6699X PDC driver instantiated",
		cfg->connector_number);

	return 0;
}

static void tps_check_and_notify_irq(void)
{
	for (int port = 0; port < NUM_PDC_TPS6699X_PORTS; port++) {
		struct pdc_data_t *data = pdc_data[port];
		struct pdc_config_t const *cfg;
		union reg_interrupt pdc_interrupt = { 0 };

		if (data == NULL || !device_is_ready(data->dev)) {
			/* Port is not in use. Skip it. */
			continue; /* LCOV_EXCL_LINE - b/406176587 */
		}

		cfg = data->dev->config;

		if (!gpio_pin_get_dt(&cfg->irq_gpios)) {
			continue;
		}

		/* Read the pending interrupt events */
		tps_rd_interrupt_event(&cfg->i2c, &pdc_interrupt);

		for (int i = 0; i < sizeof(union reg_interrupt); i++) {
			if (pdc_interrupt.raw_value[i]) {
				LOG_DBG("TI%d: pending interrupt detected",
					port);
				k_event_post(&data->pdc_event, PDC_IRQ_EVENT);
				break;
			}
		}
	}
}

static void tps_thread(void *dev, void *unused1, void *unused2)
{
	struct pdc_data_t *data = ((const struct device *)dev)->data;
	const struct pdc_config_t *cfg = ((const struct device *)dev)->config;
	bool irq_pending_for_idle = false;

	while (1) {
		smf_run_state(SMF_CTX(data));

		/* Wait for event to handle */
		data->events = k_event_wait(&data->pdc_event, PDC_ALL_EVENTS,
					    false, K_FOREVER);
		LOG_INF("TI%d: state=%s events=0x%X", cfg->connector_number,
			state_names[get_state(data)], data->events);

		k_event_clear(&data->pdc_event, PDC_INTERNAL_EVENT);

		if (data->events & PDC_IRQ_EVENT) {
			k_event_clear(&data->pdc_event, PDC_IRQ_EVENT);

			if (!check_comms_suspended()) {
				irq_pending_for_idle = true;
			}
		}

		/* We only handle IRQs on idle. */
		if (get_state(data) == ST_IDLE && irq_pending_for_idle) {
			if (handle_irqs(data) < 0) {
				k_work_reschedule(
					&data->delayed_post,
					K_MSEC(PDC_HANDLE_IRQ_RETRY_DELAY));
			} else {
				irq_pending_for_idle = false;
			}
		}
	}
}

#define PDC_DATA_STRUCT_NAME(inst) pdc_data_##inst

BUILD_ASSERT(
	EC_TASK_PRIORITY(EC_TASK_USBC_PDC_PRIO) ==
		CONFIG_USBC_PDC_TPS6699X_THREAD_PRIORITY,
	"EC_TASK_USBC_PDC_PRIO does not match USBC_PDC_TPS6699X_THREAD_PRIORITY.");

#define TPS6699X_PDC_DEFINE(inst)                                              \
	K_THREAD_STACK_DEFINE(tps6699x_thread_stack_area_##inst,               \
			      CONFIG_USBC_PDC_TPS6699X_STACK_SIZE);            \
                                                                               \
	static void create_thread_##inst(const struct device *dev)             \
	{                                                                      \
		struct pdc_data_t *data = dev->data;                           \
                                                                               \
		data->thread = k_thread_create(                                \
			&data->thread_data, tps6699x_thread_stack_area_##inst, \
			K_THREAD_STACK_SIZEOF(                                 \
				tps6699x_thread_stack_area_##inst),            \
			tps_thread, (void *)dev, 0, 0,                         \
			CONFIG_USBC_PDC_TPS6699X_THREAD_PRIORITY, K_ESSENTIAL, \
			K_FOREVER);                                            \
		k_thread_name_set(data->thread, "TPS6699X" STRINGIFY(inst));   \
	}                                                                      \
                                                                               \
	static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst);                   \
                                                                               \
	/* TODO(b/345783692): Make sure interrupt enable bits match the events \
	 * we need to respond to.                                              \
	 */                                                                    \
	static const struct pdc_config_t pdc_config##inst = {                  \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                             \
		.irq_gpios = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),           \
		.connector_number =                                            \
			USBC_PORT_FROM_PDC_DRIVER_NODE(DT_DRV_INST(inst)),     \
		.bits.command_completed = 0, /* Reserved on TI */              \
		.bits.external_supply_change = 1,                              \
		.bits.power_operation_mode_change = 1,                         \
		.bits.attention = 1,                                           \
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
		.no_fw_update = DT_INST_PROP(inst, no_fw_update),              \
		.ccd = DT_INST_PROP(inst, ccd),                                \
		.port_index_on_chip = DT_INST_PROP(inst, port_index_on_chip),  \
	};                                                                     \
                                                                               \
	DEVICE_DT_INST_DEFINE(inst, pdc_init, NULL,                            \
			      &PDC_DATA_STRUCT_NAME(inst), &pdc_config##inst,  \
			      POST_KERNEL, CONFIG_PDC_DRIVER_INIT_PRIORITY,    \
			      &pdc_driver_api);                                \
                                                                               \
	static struct pdc_data_t PDC_DATA_STRUCT_NAME(inst) = {                \
		.dev = DEVICE_DT_INST_GET(inst),                               \
	};

DT_INST_FOREACH_STATUS_OKAY(TPS6699X_PDC_DEFINE)

#define PDC_DATA_PTR_ENTRY(inst) &PDC_DATA_STRUCT_NAME(inst),

/* Populate the pdc_data struct with a pointer to each TI PDC port's device
 * struct. */
static struct pdc_data_t *const pdc_data[] = { DT_INST_FOREACH_STATUS_OKAY(
	PDC_DATA_PTR_ENTRY) };

#ifdef CONFIG_USBC_PDC_DRIVEN_CCD
/* If PDC-driven CCD is used, one of the PDC driver nodes for each driver compat
 * type must be marked with the `ccd` property.
 */
CHECK_ONE_CCD_PORT_COUNT_FOR_DRIVER();
#endif /* CONFIG_USBC_PDC_DRIVEN_CCD */

#ifdef CONFIG_ZTEST
/*
 * Wait for drivers to become idle.
 */
/* LCOV_EXCL_START */
bool pdc_tps6699x_test_idle_wait(void)
{
	int num_finished;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(20 * 100));

	while (!sys_timepoint_expired((timeout))) {
		num_finished = 0;

		k_msleep(100);
		for (int port = 0; port < ARRAY_SIZE(pdc_data); port++) {
			if (!device_is_ready(pdc_data[port]->dev)) {
				/* This port is not in use. Consider it finished
				 * so we do not wait on it. */
				num_finished++;
			}
			if (get_state(pdc_data[port]) == ST_IDLE &&
			    pdc_data[port]->cmd == CMD_NONE) {
				/* Driver is in the idle state with no pending
				 * commands. */
				num_finished++;
			}
		}

		if (num_finished == ARRAY_SIZE(pdc_data)) {
			return true;
		}
	}

	return false;
}
/* LCOV_EXCL_STOP */

#endif /* CONFIG_ZTEST */
