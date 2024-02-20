/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * PD Controller subsystem
 */

#define DT_DRV_COMPAT named_usbc_port

#include "charge_manager.h"
#include "charge_state.h"
#include "hooks.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>

#include <drivers/pdc.h>
#include <usbc/utils.h>

LOG_MODULE_REGISTER(pdc_power_mgmt);

/**
 * @brief Event triggered by sending an internal command
 */
#define PDC_SM_EVENT BIT(0)

/**
 * @brief Time delay before running the state machine loop
 */
#define LOOP_DELAY_MS 25

/**
 * @brief maximum number of times to try and send a command, or wait for a
 * public API command to execute (Time is 2s)
 *
 */
#define WAIT_MAX (2000 / LOOP_DELAY_MS)

/**
 * @brief maximum number of times to try and send a command, or wait for a
 * public API command to execute (Time is 2s)
 *
 */
#define CMD_RESEND_MAX 2

/**
 * @brief maximum number of PDOs
 */
#define PDO_NUM 7

/**
 * @brief PDC driver commands
 */
enum pdc_cmd_t {
	/** CMD_PDC_NONE */
	CMD_PDC_NONE,
	/** CMD_PDC_RESET */
	CMD_PDC_RESET,
	/** CMD_PDC_SET_POWER_LEVEL */
	CMD_PDC_SET_POWER_LEVEL,
	/** CMD_PDC_SET_CCOM */
	CMD_PDC_SET_CCOM,
	/** CMD_PDC_GET_PDOS */
	CMD_PDC_GET_PDOS,
	/** CMD_PDC_GET_RDO */
	CMD_PDC_GET_RDO,
	/** CMD_PDC_SET_RDO */
	CMD_PDC_SET_RDO,
	/** CMD_PDC_GET_VBUS_VOLTAGE */
	CMD_PDC_GET_VBUS_VOLTAGE,
	/** CMD_PDC_SET_SINK_PATH */
	CMD_PDC_SET_SINK_PATH,
	/** CMD_PDC_READ_POWER_LEVEL */
	CMD_PDC_READ_POWER_LEVEL,
	/** CMD_PDC_GET_INFO */
	CMD_PDC_GET_INFO,
	/** CMD_PDC_GET_CONNECTOR_CAPABILITY */
	CMD_PDC_GET_CONNECTOR_CAPABILITY,
	/** CMD_PDC_SET_UOR */
	CMD_PDC_SET_UOR,
	/** CMD_PDC_SET_PDR */
	CMD_PDC_SET_PDR,
	/** CMD_PDC_GET_CONNECTOR_STATUS */
	CMD_PDC_GET_CONNECTOR_STATUS,

	/** CMD_PDC_COUNT */
	CMD_PDC_COUNT
};

/**
 * @brief Send Local States
 */
enum send_cmd_state_t {
	/** SEND_CMD_START_ENTRY */
	SEND_CMD_START_ENTRY,
	/** SEND_CMD_START_RUN */
	SEND_CMD_START_RUN,
	/** SEND_CMD_WAIT_ENTRY */
	SEND_CMD_WAIT_ENTRY,
	/** SEND_CMD_WAIT_RUN */
	SEND_CMD_WAIT_RUN,
	/** SEND_CMD_WAIT_EXIT */
	SEND_CMD_WAIT_EXIT,
};

/**
 * @brief Command type
 */
struct cmd_t {
	/** Command to send */
	enum pdc_cmd_t cmd;
	/** True if command is pending */
	bool pending;
	/** True if command failed to send */
	bool error;
};

/**
 * @brief Send command type
 */
struct send_cmd_t {
	/** Send command local state */
	enum send_cmd_state_t local_state;
	/* Wait counter used in local wait state */
	uint16_t wait_counter;
	/* Command resend counter */
	uint8_t resend_counter;
	/* Command sent from public API */
	struct cmd_t public;
	/* Command sent from internal API */
	struct cmd_t intern;
};

/**
 * @brief SNK Attached Local States
 */
enum snk_attached_local_state_t {
	/** SNK_ATTACHED_GET_CONNECTOR_CAPABILITY */
	SNK_ATTACHED_GET_CONNECTOR_CAPABILITY,
	/** SNK_ATTACHED_SET_DR_SWAP_POLICY */
	SNK_ATTACHED_SET_DR_SWAP_POLICY,
	/** SNK_ATTACHED_SET_PR_SWAP_POLICY */
	SNK_ATTACHED_SET_PR_SWAP_POLICY,
	/** SNK_ATTACHED_READ_POWER_LEVEL */
	SNK_ATTACHED_READ_POWER_LEVEL,
	/** SNK_ATTACHED_GET_PDOS */
	SNK_ATTACHED_GET_PDOS,
	/** SNK_ATTACHED_GET_RDO */
	SNK_ATTACHED_GET_RDO,
	/** SNK_ATTACHED_SET_SINK_PATH_ON */
	SNK_ATTACHED_SET_SINK_PATH_ON,
	/** SNK_ATTACHED_START_CHARGING */
	SNK_ATTACHED_START_CHARGING,
	/** SNK_ATTACHED_RUN */
	SNK_ATTACHED_RUN,
};

/**
 * @brief SRC Attached Local States
 */
enum src_attached_local_state_t {
	/** SRC_ATTACHED_GET_CONNECTOR_CAPABILITY */
	SRC_ATTACHED_GET_CONNECTOR_CAPABILITY,
	/** SRC_ATTACHED_SET_DR_SWAP_POLICY */
	SRC_ATTACHED_SET_DR_SWAP_POLICY,
	/** SRC_ATTACHED_SET_PR_SWAP_POLICY */
	SRC_ATTACHED_SET_PR_SWAP_POLICY,
	/** SRC_ATTACHED_RUN */
	SRC_ATTACHED_RUN,
};

/**
 * @brief Unattached Local States
 */
enum unattached_local_state_t {
	/** UNATTACHED_RUN */
	UNATTACHED_RUN,
};

/**
 * @brief CCI Event Flags
 */
enum cci_flag_t {
	/** CCI_BUSY */
	CCI_BUSY,
	/** CCI_ERROR */
	CCI_ERROR,
	/** CCI_CMD_COMPLETED */
	CCI_CMD_COMPLETED,
	/** CCI_EVENT */
	CCI_EVENT,

	/** CCI_FLAGS_COUNT */
	CCI_FLAGS_COUNT
};

/**
 * @brief State Machine States
 */
enum pdc_state_t {
	/** PDC_INIT */
	PDC_INIT,
	/** PDC_UNATTACHED */
	PDC_UNATTACHED,
	/** PDC_SNK_ATTACHED */
	PDC_SNK_ATTACHED,
	/** PDC_SRC_ATTACHED */
	PDC_SRC_ATTACHED,
	/** PDC_SEND_CMD_START */
	PDC_SEND_CMD_START,
	/** PDC_SEND_CMD_WAIT */
	PDC_SEND_CMD_WAIT,
	/** PDC_SRC_SNK_TYPEC_ONLY */
	PDC_SRC_SNK_TYPEC_ONLY,
};

/**
 * @brief PDC Command Names
 */
static const char *const pdc_cmd_names[] = {
	[CMD_PDC_NONE] = "",
	[CMD_PDC_RESET] = "PDC_RESET",
	[CMD_PDC_SET_POWER_LEVEL] = "PDC_SET_POWER_LEVEL",
	[CMD_PDC_SET_CCOM] = "PDC_SET_CCOM",
	[CMD_PDC_GET_PDOS] = "PDC_GET_PDOS",
	[CMD_PDC_GET_RDO] = "PDC_GET_RDO",
	[CMD_PDC_SET_RDO] = "PDC_SET_RDO",
	[CMD_PDC_GET_VBUS_VOLTAGE] = "PDC_GET_VBUS_VOLTAGE",
	[CMD_PDC_SET_SINK_PATH] = "PDC_SET_SINK_PATH",
	[CMD_PDC_READ_POWER_LEVEL] = "PDC_READ_POWER_LEVEL",
	[CMD_PDC_GET_INFO] = "PDC_GET_INFO",
	[CMD_PDC_GET_CONNECTOR_CAPABILITY] = "PDC_GET_CONNECTOR_CAPABILITY",
	[CMD_PDC_SET_UOR] = "PDC_SET_UOR",
	[CMD_PDC_SET_PDR] = "PDC_SET_PDR",
	[CMD_PDC_GET_CONNECTOR_STATUS] = "PDC_GET_CONNECTOR_STATUS",
};

/**
 * @brief State Machine State Names
 */
static const char *const pdc_state_names[] = {
	[PDC_INIT] = "PDC Init",
	[PDC_UNATTACHED] = "Unattached",
	[PDC_SNK_ATTACHED] = "Attached.SNK",
	[PDC_SRC_ATTACHED] = "Attached.SRC",
	[PDC_SEND_CMD_START] = "SendCmdStart",
	[PDC_SEND_CMD_WAIT] = "SendCmdWait",
	[PDC_SRC_SNK_TYPEC_ONLY] = "TypeCAttached",
};

/**
 * @brief Unattached policy flags
 */
enum policy_unattached_t {
	/** UNA_POLICY_TCC */
	UNA_POLICY_TCC,
	/** UNA_POLICY_CC_MODE */
	UNA_POLICY_CC_MODE,
	/** UNA_POLICY_COUNT */
	UNA_POLICY_COUNT,
};

/**
 * @brief Unattached policy object
 */
struct pdc_unattached_policy_t {
	/** Unattached policy flags */
	ATOMIC_DEFINE(flags, UNA_POLICY_COUNT);
	/** Type-C current */
	enum usb_typec_current_t tcc;
	/** CC Operation Mode */
	enum ccom_t cc_mode;
	/** DRP Operation Mode */
	enum drp_mode_t drp_mode;
};

/**
 * @brief Sink policy flags
 */
enum policy_snk_attached_t {
	/** Request a new power level */
	SNK_POLICY_NEW_POWER_REQUEST,
	/** Enables swap to Source */
	SNK_POLICY_SWAP_TO_SRC,
	/** Selects the low power PDO on connect */
	SNK_POLICY_REQUEST_LOW_POWER_PDO,
	/** Selects the highest powered PDO on connect */
	SNK_POLICY_REQUEST_HIGH_POWER_PDO,

	/** SNK_POLICY_COUNT */
	SNK_POLICY_COUNT,
};

/**
 * @brief Attached state flags
 */
enum attached_flag_t {
	/** UNATTACHED_FLAG */
	UNATTACHED_FLAG,
	/** SRC_ATTACHED_FLAG */
	SRC_ATTACHED_FLAG,
	/** SNK_ATTACHED_FLAG */
	SNK_ATTACHED_FLAG,
	/** SNK_SRC_ATTACHED_TYPEC_ONLY_FLAG */
	SNK_SRC_ATTACHED_TYPEC_ONLY_FLAG,
};

/**
 * @brief Sink attached policy object
 */
struct pdc_snk_attached_policy_t {
	/** SNK Attached policy flags */
	ATOMIC_DEFINE(flags, SNK_POLICY_COUNT);
	/** Currently active PDO */
	uint32_t pdo;
	/** PDOs supported by the Source */
	uint32_t pdos[PDO_NUM];
	/** PDO count */
	uint8_t pdo_count;
	/** Sent RDO */
	uint32_t rdo;
	/** New RDO to send */
	uint32_t rdo_to_send;
};

/**
 * @brief Source attached policy flags
 */
enum policy_src_attached_t {
	/** Enables swap to Sink */
	SRC_POLICY_SWAP_TO_SNK,

	/** SRC_POLICY_COUNT */
	SRC_POLICY_COUNT
};

/**
 * @brief Source attached policy object
 */
struct pdc_src_attached_policy_t {
	/** SRC Attached policy flags */
	ATOMIC_DEFINE(flags, SRC_POLICY_COUNT);
};

/**
 * @brief PDC Port object
 */
struct pdc_port_t {
	/** State machine context */
	struct smf_ctx ctx;
	/** Subsystem device */
	const struct device *dev;
	/** PDC device */
	const struct device *pdc;

	/** CCI flags */
	ATOMIC_DEFINE(cci_flags, CCI_FLAGS_COUNT);
	/** PDC Cmd flags */
	ATOMIC_DEFINE(pdc_cmd_flags, CMD_PDC_COUNT);

	/** Unattached local state variable */
	enum unattached_local_state_t unattached_local_state;
	/** Last unattached local state variable */
	enum unattached_local_state_t unattached_last_state;
	/** Sink attached local state variable */
	enum snk_attached_local_state_t snk_attached_local_state;
	/** Last Sink attached local state variable */
	enum snk_attached_local_state_t snk_attached_last_state;
	/** Source attached local state variable */
	enum src_attached_local_state_t src_attached_local_state;
	/** Last Source attached local state variable */
	enum src_attached_local_state_t src_attached_last_state;
	/** State machine run event */
	struct k_event sm_event;

	/** Transitioning from last_state */
	enum pdc_state_t last_state;
	/* Transitioning to next state */
	enum pdc_state_t next_state;
	/* Return state from sending a command */
	enum pdc_state_t send_cmd_return_state;
	/** PDC Unattached policy */
	struct pdc_unattached_policy_t una_policy;
	/** PDC Sink Attached policy */
	struct pdc_snk_attached_policy_t snk_policy;
	/** PDC Source Attached policy */
	struct pdc_src_attached_policy_t src_policy;

	/** PDC version and other information */
	struct pdc_info_t info;
	/** Public API block counter */
	uint8_t block_counter;
	/** Command mutex */
	struct k_mutex mtx;
	/** PDC command to send */
	struct send_cmd_t send_cmd;
	/** Pointer to current pending command */
	struct cmd_t *cmd;
	/** CCAPS temp variable used with CMD_PDC_GET_CONNECTOR_CAPABILITY
	 * command */
	union connector_capability_t ccaps;
	/** CONNECTOR_STATUS temp variable used with CONNECTOR_GET_STATUS
	 * command */
	struct connector_status_t connector_status;
	/** SINK_PATH_EN temp variable used with CMD_PDC_SET_SINK_PATH command
	 */
	bool sink_path_en;
	/** VBUS temp variable used with CMD_PDC_GET_VBUS_VOLTAGE command */
	uint16_t vbus;
	/** UOR variable used with CMD_PDC_SET_UOR command */
	union uor_t uor;
	/** PDR variable used with CMD_PDC_SET_PDR command */
	union pdr_t pdr;
	/** True if battery can charge from this port */
	bool active_charge;
	/** True if in source attached state */
	bool attached_src;
	/** True if in sink attached state */
	bool attached_snk;
	/** True if in typec only attached state */
	bool attached_snk_src_typec_only;
	/** True if attached device is PD Capable */
	bool pd_capable;
};

/**
 * @brief Subsystem PDC Data
 */
struct pdc_data_t {
	/** This port's thread */
	k_tid_t thread;
	/** This port thread's data */
	struct k_thread thread_data;
	/** Port data */
	struct pdc_port_t port;
};

/**
 * @brief Subsystem PDC Config
 */
struct pdc_config_t {
	/** Port number for the connector */
	uint8_t connector_num;
	/**
	 * The usbc stack initializes this pointer that creates the
	 * main thread for this port
	 */
	void (*create_thread)(const struct device *dev);
};

static const struct smf_state pdc_states[];
static enum pdc_state_t get_pdc_state(struct pdc_port_t *port);
static int pdc_subsys_init(const struct device *dev);
static void send_cmd_init(struct pdc_port_t *port);
static void queue_internal_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd);
static int queue_public_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd);

/**
 * @brief PDC thread
 */
static ALWAYS_INLINE void pdc_thread(void *pdc_dev, void *unused1,
				     void *unused2)
{
	const struct device *dev = (const struct device *)pdc_dev;
	struct pdc_data_t *data = dev->data;
	struct pdc_port_t *port = &data->port;

	while (1) {
		/* Wait for timeout or event */
		k_event_wait(&port->sm_event, PDC_SM_EVENT, false,
			     K_MSEC(LOOP_DELAY_MS));
		/* Run port connection state machine */
		smf_run_state(&port->ctx);
	}
}

#define PDC_SUBSYS_INIT(inst)                                                \
	K_THREAD_STACK_DEFINE(my_stack_area_##inst,                          \
			      CONFIG_PDC_POWER_MGMT_STACK_SIZE);             \
                                                                             \
	static void create_thread_##inst(const struct device *dev)           \
	{                                                                    \
		struct pdc_data_t *data = dev->data;                         \
                                                                             \
		data->thread = k_thread_create(                              \
			&data->thread_data, my_stack_area_##inst,            \
			K_THREAD_STACK_SIZEOF(my_stack_area_##inst),         \
			pdc_thread, (void *)dev, 0, 0,                       \
			CONFIG_PDC_POWER_MGMT_THREAD_PRIORTY, K_ESSENTIAL,   \
			K_NO_WAIT);                                          \
		k_thread_name_set(data->thread,                              \
				  "PDC Power Mgmt" STRINGIFY(inst));         \
	}                                                                    \
                                                                             \
	static struct pdc_data_t data_##inst = {                             \
		.port.dev = DEVICE_DT_INST_GET(inst), /* Initial policy read \
							 from device tree */ \
		.port.pdc = DEVICE_DT_GET(DT_INST_PROP(inst, pdc)),          \
		.port.una_policy.tcc = DT_STRING_TOKEN(                      \
			DT_INST_PROP(inst, policy), unattached_rp_value),    \
		.port.una_policy.cc_mode = DT_STRING_TOKEN(                  \
			DT_INST_PROP(inst, policy), unattached_cc_mode),     \
		.port.una_policy.drp_mode = DT_STRING_TOKEN(                 \
			DT_INST_PROP(inst, policy), unattached_try),         \
	};                                                                   \
                                                                             \
	static struct pdc_config_t config_##inst = {                         \
		.connector_num = USBC_PORT_NEW(DT_DRV_INST(inst)),           \
		.create_thread = create_thread_##inst,                       \
	};                                                                   \
                                                                             \
	DEVICE_DT_INST_DEFINE(inst, &pdc_subsys_init, NULL, &data_##inst,    \
			      &config_##inst, POST_KERNEL,                   \
			      CONFIG_PDC_POWER_MGMT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PDC_SUBSYS_INIT)

#define PDC_DATA_INIT(inst) [USBC_PORT_NEW(DT_DRV_INST(inst))] = &data_##inst,

/**
 * @brief data structure used by public API to map port number to PDC_DATA.
 *        The port number is used to index the array.
 */
static struct pdc_data_t *pdc_data[] = { DT_INST_FOREACH_STATUS_OKAY(
	PDC_DATA_INIT) };

static enum pdc_state_t get_pdc_state(struct pdc_port_t *port)
{
	return port->ctx.current - &pdc_states[0];
}

static void set_pdc_state(struct pdc_port_t *port, enum pdc_state_t next_state)
{
	if (get_pdc_state(port) != next_state) {
		port->last_state = get_pdc_state(port);
		port->next_state = next_state;
		smf_set_state(SMF_CTX(port), &pdc_states[next_state]);
	}
}

static void print_current_pdc_state(struct pdc_port_t *port)
{
	const struct pdc_config_t *const config = port->dev->config;

	LOG_INF("C%d: %s", config->connector_num,
		pdc_state_names[get_pdc_state(port)]);
}

static void send_cmd_init(struct pdc_port_t *port)
{
	port->send_cmd.public.cmd = CMD_PDC_NONE;
	port->send_cmd.public.error = false;
	port->send_cmd.public.pending = false;
	port->send_cmd.intern.cmd = CMD_PDC_NONE;
	port->send_cmd.intern.error = false;
	port->send_cmd.intern.pending = false;
	port->send_cmd.local_state = SEND_CMD_START_ENTRY;
}

/**
 * @brief Run a command started by a public api function call
 */
static void send_pending_public_commands(struct pdc_port_t *port)
{
	/* Send a pending public command */
	if (port->send_cmd.public.pending) {
		set_pdc_state(port, PDC_SEND_CMD_START);
	}
}

/**
 * @brief Limits the charge current to zero and invalidates and received Source
 * PDOS. This function also seeds the charger.
 */
static void invalidate_charger_settings(struct pdc_port_t *port)
{
	const struct pdc_config_t *const config = port->dev->config;

	typec_set_input_current_limit(config->connector_num, 0, 0);
	pd_set_input_current_limit(config->connector_num, 0, 0);
	charge_manager_set_ceil(config->connector_num, CEIL_REQUESTOR_PD,
				CHARGE_CEIL_NONE);
	charge_manager_update_dualrole(config->connector_num, CAP_UNKNOWN);

	/* Invalidate PDOS */
	port->snk_policy.pdo = 0;
	memset(port->snk_policy.pdos, 0, sizeof(uint32_t) * PDO_NUM);
	port->snk_policy.pdo_count = 0;
}

/**
 * @brief Callers of this function should return immediately because the PDC
 * state is changed.
 */
static int queue_public_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd)
{
	/* Don't send another public initiated command if one is already pending
	 */
	if (port->send_cmd.public.pending) {
		return -EBUSY;
	}

	k_mutex_lock(&port->mtx, K_FOREVER);
	port->send_cmd.public.cmd = pdc_cmd;
	port->send_cmd.public.error = false;
	port->send_cmd.public.pending = true;
	k_mutex_unlock(&port->mtx);
	k_event_post(&port->sm_event, PDC_SM_EVENT);
	return 0;
}

/**
 * @brief Callers of this function should return immediately because the PDC
 * state is changed.
 */
static void queue_internal_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd)
{
	k_mutex_lock(&port->mtx, K_FOREVER);
	port->send_cmd.intern.cmd = pdc_cmd;
	port->send_cmd.intern.error = false;
	port->send_cmd.intern.pending = true;
	k_mutex_unlock(&port->mtx);
	k_event_post(&port->sm_event, PDC_SM_EVENT);

	set_pdc_state(port, PDC_SEND_CMD_START);
}

/**
 * @brief Callers of this function should return immediately because the PDC
 * state is changed.
 */
static void send_snk_path_en_cmd(struct pdc_port_t *port, bool en)
{
	port->sink_path_en = en;
	queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
}

/**
 * @brief Tracks the attached state of the state machine
 */
static void set_attached_flag(struct pdc_port_t *port,
			      enum attached_flag_t flag)
{
	k_mutex_lock(&port->mtx, K_FOREVER);
	switch (flag) {
	case SRC_ATTACHED_FLAG:
		port->attached_snk_src_typec_only = false;
		port->attached_snk = false;
		port->attached_src = true;
		break;
	case SNK_ATTACHED_FLAG:
		port->attached_snk_src_typec_only = false;
		port->attached_snk = true;
		port->attached_src = false;
		break;
	case SNK_SRC_ATTACHED_TYPEC_ONLY_FLAG:
		port->attached_snk_src_typec_only = true;
		port->attached_snk = false;
		port->attached_src = false;
		break;
	default:
		port->attached_snk_src_typec_only = false;
		port->attached_snk = false;
		port->attached_src = false;
		break;
	}
	k_mutex_unlock(&port->mtx);
}

/**
 * @brief This function should only be called after the completion of the
 * GET_CONNECTOR_STATUS command. It reads the connect_status,
 * power_operation_mode, and power_direction bit to determine which state should
 * be entered.
 * NOTE: This function changes state, so a return should be added
 * after it's called.
 */
static void handle_connector_status(struct pdc_port_t *port)
{
	if (!port->connector_status.connect_status) {
		port->pd_capable = false;
		/* Port is not connected */
		set_pdc_state(port, PDC_UNATTACHED);
	} else {
		if (port->connector_status.power_operation_mode ==
		    PD_OPERATION) {
			port->pd_capable = true;
			if (port->connector_status.power_direction) {
				/* Port partner is a sink device
				 */
				set_pdc_state(port, PDC_SRC_ATTACHED);
			} else {
				/* Port partner is a source
				 * device */
				set_pdc_state(port, PDC_SNK_ATTACHED);
			}
		} else {
			port->pd_capable = false;
			set_pdc_state(port, PDC_SRC_SNK_TYPEC_ONLY);
		}
	}
}

static void run_unattached_policies(struct pdc_port_t *port)
{
	if (atomic_test_and_clear_bit(port->una_policy.flags,
				      UNA_POLICY_CC_MODE)) {
		/* Set CC PULL Resistor and TrySrc or TrySnk */
		queue_internal_cmd(port, CMD_PDC_SET_CCOM);
		return;
	} else if (atomic_test_and_clear_bit(port->una_policy.flags,
					     UNA_POLICY_TCC)) {
		/* Set RP current policy */
		queue_internal_cmd(port, CMD_PDC_SET_POWER_LEVEL);
		return;
	}

	send_pending_public_commands(port);
}

static void run_snk_policies(struct pdc_port_t *port)
{
	if (atomic_test_and_clear_bit(port->snk_policy.flags,
				      SNK_POLICY_SWAP_TO_SRC)) {
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	}

	send_pending_public_commands(port);
}

static void run_src_policies(struct pdc_port_t *port)
{
	if (atomic_test_and_clear_bit(port->src_policy.flags,
				      SRC_POLICY_SWAP_TO_SNK)) {
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	}

	send_pending_public_commands(port);
}

/**
 * @brief Entering unattached state
 */
static void pdc_unattached_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	set_attached_flag(port, UNATTACHED_FLAG);

	port->send_cmd.intern.pending = false;

	invalidate_charger_settings(port);

	if (get_pdc_state(port) != port->send_cmd_return_state) {
		port->unattached_local_state = UNATTACHED_RUN;
	}
}

/**
 * @brief Run unattached state
 */
static void pdc_unattached_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* The CCI_EVENT is set on a connector disconnect, so check the
	 * connector status and take the appropriate action. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	/* TODO: Limit the number of retries and try to reset PDC, then
	 * ultimately disable port */
	if (port->send_cmd.intern.error) {
		/* The last command could not be sent, so send it again */
		port->unattached_local_state = port->unattached_last_state;
	} else {
		port->unattached_last_state = port->unattached_local_state;
	}

	switch (port->unattached_local_state) {
	case UNATTACHED_RUN:
		run_unattached_policies(port);
		break;
	}
}

/**
 * @brief Entering source attached state
 */
static void pdc_src_attached_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	port->send_cmd.intern.pending = false;

	invalidate_charger_settings(port);

	if (get_pdc_state(port) != port->send_cmd_return_state) {
		port->src_attached_local_state =
			SRC_ATTACHED_GET_CONNECTOR_CAPABILITY;
	}
}

/**
 * @brief Run source attached state
 */
static void pdc_src_attached_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* The CCI_EVENT is set on a connector disconnect, so check the
	 * connector status and take the appropriate action. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	/* TODO: Limit the number of retries and try to reset PDC, then
	 * ultimately disable port */
	if (port->send_cmd.intern.error) {
		/* The last command could not be sent, so send it again */
		port->src_attached_local_state = port->src_attached_last_state;
	} else {
		port->src_attached_last_state = port->src_attached_local_state;
	}

	/* TODO: b/319643480 - Brox: implement SRC policies */

	switch (port->src_attached_local_state) {
	case SRC_ATTACHED_GET_CONNECTOR_CAPABILITY:
		port->src_attached_local_state =
			SRC_ATTACHED_SET_DR_SWAP_POLICY;
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_CAPABILITY);
		return;
	case SRC_ATTACHED_SET_DR_SWAP_POLICY:
		port->src_attached_local_state =
			SRC_ATTACHED_SET_PR_SWAP_POLICY;
		port->uor.accept_dr_swap = 1; /* TODO read from DT */
		queue_internal_cmd(port, CMD_PDC_SET_UOR);
		return;
	case SRC_ATTACHED_SET_PR_SWAP_POLICY:
		port->src_attached_local_state = SRC_ATTACHED_RUN;
		port->pdr.accept_pr_swap = 1; /* TODO read from DT */
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	case SRC_ATTACHED_RUN:
		set_attached_flag(port, SRC_ATTACHED_FLAG);
		run_src_policies(port);
		break;
	}
}

/**
 * @brief Entering sink attached state
 */
static void pdc_snk_attached_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	port->send_cmd.intern.pending = false;
	if (get_pdc_state(port) != port->send_cmd_return_state) {
		port->snk_attached_local_state =
			SNK_ATTACHED_GET_CONNECTOR_CAPABILITY;
	}
}

/**
 * @brief Run sink attached state.
 */
static void pdc_snk_attached_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *const config = port->dev->config;
	uint32_t max_ma, max_mv, max_mw;

	/* The CCI_EVENT is set on a connector disconnect, so check the
	 * connector status and take the appropriate action. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	/* TODO: Limit the number of retries and try to reset PDC, then
	 * ultimately disable port */
	if (port->send_cmd.intern.error) {
		/* The last command could not be sent, so send it again */
		port->snk_attached_local_state = port->snk_attached_last_state;
	} else {
		port->snk_attached_last_state = port->snk_attached_local_state;
	}

	switch (port->snk_attached_local_state) {
	case SNK_ATTACHED_GET_CONNECTOR_CAPABILITY:
		port->snk_attached_local_state =
			SNK_ATTACHED_SET_DR_SWAP_POLICY;
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_CAPABILITY);
		return;
	case SNK_ATTACHED_SET_DR_SWAP_POLICY:
		port->snk_attached_local_state =
			SNK_ATTACHED_SET_PR_SWAP_POLICY;
		port->uor.accept_dr_swap = 1; /* TODO read from DT */
		queue_internal_cmd(port, CMD_PDC_SET_UOR);
		return;
	case SNK_ATTACHED_SET_PR_SWAP_POLICY:
		port->snk_attached_local_state = SNK_ATTACHED_READ_POWER_LEVEL;
		port->pdr.accept_pr_swap = 1; /* TODO read from DT */
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	case SNK_ATTACHED_READ_POWER_LEVEL:
		port->snk_attached_local_state = SNK_ATTACHED_GET_PDOS;
		queue_internal_cmd(port, CMD_PDC_READ_POWER_LEVEL);
		return;
	case SNK_ATTACHED_GET_PDOS:
		port->snk_attached_local_state = SNK_ATTACHED_GET_RDO;
		queue_internal_cmd(port, CMD_PDC_GET_PDOS);
		return;
	case SNK_ATTACHED_GET_RDO:
		/* Test if battery can be charged from this port */
		if (port->active_charge) {
			port->snk_attached_local_state =
				SNK_ATTACHED_SET_SINK_PATH_ON;
		} else {
			port->snk_attached_local_state = SNK_ATTACHED_RUN;
		}
		queue_internal_cmd(port, CMD_PDC_GET_RDO);
		return;
	case SNK_ATTACHED_SET_SINK_PATH_ON:
		port->snk_attached_local_state = SNK_ATTACHED_START_CHARGING;
		send_snk_path_en_cmd(port, true);
		return;
	case SNK_ATTACHED_START_CHARGING:
		for (int i = 0; i < PDO_NUM; i++) {
			LOG_INF("PDO%d: %08x, %d %d", i,
				port->snk_policy.pdos[i],
				PDO_FIXED_GET_VOLT(port->snk_policy.pdos[i]),
				PDO_FIXED_GET_CURR(port->snk_policy.pdos[i]));
		}

		LOG_INF("RDO: %d", RDO_POS(port->snk_policy.rdo));
		port->snk_policy.pdo =
			port->snk_policy.pdos[RDO_POS(port->snk_policy.rdo) - 1];

		/* Extract Current, Voltage, and calculate Power */
		max_ma = PDO_FIXED_GET_CURR(port->snk_policy.pdo);
		max_mv = PDO_FIXED_GET_VOLT(port->snk_policy.pdo);
		max_mw = max_ma * max_mv / 1000;

		LOG_INF("Charging ON PORT%d\n", config->connector_num);
		LOG_INF("PDO: %08x", port->snk_policy.pdo);
		LOG_INF("V: %d", max_mv);
		LOG_INF("C: %d", max_ma);
		LOG_INF("P: %d", max_mw);

		pd_set_input_current_limit(config->connector_num, max_ma,
					   max_mv);
		charge_manager_set_ceil(config->connector_num,
					CEIL_REQUESTOR_PD, max_ma);

		if (((PDO_GET_TYPE(port->snk_policy.pdo) == 0) &&
		     (!(port->snk_policy.pdo & PDO_FIXED_GET_DRP) ||
		      (port->snk_policy.pdo &
		       PDO_FIXED_GET_UNCONSTRAINED_PWR))) ||
		    (max_mw >= PD_DRP_CHARGE_POWER_MIN)) {
			charge_manager_update_dualrole(config->connector_num,
						       CAP_DEDICATED);
		} else {
			charge_manager_update_dualrole(config->connector_num,
						       CAP_DUALROLE);
		}

		port->snk_attached_local_state = SNK_ATTACHED_RUN;
		/* fall-through */
		__attribute__((fallthrough));
	case SNK_ATTACHED_RUN:
		set_attached_flag(port, SNK_ATTACHED_FLAG);
		run_snk_policies(port);
		break;
	}
}

static void pdc_send_cmd_start_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	port->send_cmd_return_state = port->last_state;
	port->send_cmd.wait_counter = 0;

	if (port->send_cmd.intern.pending) {
		port->cmd = &port->send_cmd.intern;
	} else {
		port->cmd = &port->send_cmd.public;
	}

	k_event_clear(&port->sm_event, PDC_SM_EVENT);
}

static int send_pdc_cmd(struct pdc_port_t *port)
{
	int rv;

	/* Send PDC command via driver API */
	switch (port->cmd->cmd) {
	case CMD_PDC_RESET:
		rv = pdc_reset(port->pdc);
		break;
	case CMD_PDC_GET_INFO:
		rv = pdc_get_info(port->pdc, &port->info);
		break;
	case CMD_PDC_SET_POWER_LEVEL:
		rv = pdc_set_power_level(port->pdc, port->una_policy.tcc);
		break;
	case CMD_PDC_SET_CCOM:
		rv = pdc_set_ccom(port->pdc, port->una_policy.cc_mode,
				  port->una_policy.drp_mode);
		break;
	case CMD_PDC_GET_PDOS:
		rv = pdc_get_pdos(port->pdc, SOURCE_PDO, PDO_OFFSET_0, PDO_NUM,
				  true, &port->snk_policy.pdos[0]);
		break;
	case CMD_PDC_GET_RDO:
		rv = pdc_get_rdo(port->pdc, &port->snk_policy.rdo);
		break;
	case CMD_PDC_SET_RDO:
		rv = pdc_set_rdo(port->pdc, port->snk_policy.rdo_to_send);
		break;
	case CMD_PDC_GET_VBUS_VOLTAGE:
		rv = pdc_get_vbus_voltage(port->pdc, &port->vbus);
		break;
	case CMD_PDC_SET_SINK_PATH:
		rv = pdc_set_sink_path(port->pdc, port->sink_path_en);
		break;
	case CMD_PDC_READ_POWER_LEVEL:
		rv = pdc_read_power_level(port->pdc);
		break;
	case CMD_PDC_GET_CONNECTOR_CAPABILITY:
		rv = pdc_get_connector_capability(port->pdc, &port->ccaps);
		break;
	case CMD_PDC_SET_UOR:
		rv = pdc_set_uor(port->pdc, port->uor);
		break;
	case CMD_PDC_SET_PDR:
		rv = pdc_set_pdr(port->pdc, port->pdr);
		break;
	case CMD_PDC_GET_CONNECTOR_STATUS:
		rv = pdc_get_connector_status(port->pdc,
					      &port->connector_status);
		break;
	default:
		LOG_ERR("Invalid command: %d", port->cmd->cmd);
		return -EIO;
	}

	if (rv) {
		LOG_DBG("Unable to send command: %s",
			pdc_cmd_names[port->cmd->cmd]);
	}

	return rv;
}

static void pdc_send_cmd_start_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	int rv;

	rv = send_pdc_cmd(port);
	if (rv) {
		LOG_DBG("Unable to send command: %s",
			pdc_cmd_names[port->cmd->cmd]);
	}

	/*
	 * If the PDC is still processing a command (not in the IDLE state),
	 * then will remain in this state and CCI_CMD_COMPLETED can be set via
	 * the cci_event_cb function when the PDC driver finishes with the
	 * previous command. This flag is only meaningful for the command that
	 * was just sent to the PDC.
	 */
	atomic_clear_bit(port->cci_flags, CCI_CMD_COMPLETED);

	/* Test if command was successful. If not, try again until max
	 * retries is reached */
	if (rv) {
		port->send_cmd.wait_counter++;
		if (port->send_cmd.wait_counter > WAIT_MAX) {
			/* Could not send command: TODO handle error */
			LOG_INF("Command (%s) retry timeout",
				pdc_cmd_names[port->cmd->cmd]);
			port->cmd->error = true;
			port->cmd->pending = false;
			set_pdc_state(port, port->send_cmd_return_state);
		}
		return;
	}

	set_pdc_state(port, PDC_SEND_CMD_WAIT);
}

static void pdc_send_cmd_wait_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);
	port->send_cmd.wait_counter = 0;
	port->send_cmd.resend_counter = 0;
}

static void pdc_send_cmd_wait_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* Wait for command status notification from driver */

	/*
	 * On a PDC_RESET, the PDC initiates an initializtion and the
	 * pdc_is_init_done() function is called to check if the initialization
	 * is complete
	 */
	if (port->cmd->cmd == CMD_PDC_RESET) {
		if (pdc_is_init_done(port->pdc)) {
			port->cmd->error = false;
			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
	} else if (atomic_test_and_clear_bit(port->cci_flags, CCI_BUSY)) {
		LOG_DBG("CCI_BUSY");
	} else if (atomic_test_and_clear_bit(port->cci_flags, CCI_ERROR)) {
		LOG_DBG("CCI_ERROR");
		/* The PDC may set both error and complete bit */
		atomic_clear_bit(port->cci_flags, CCI_CMD_COMPLETED);

		/*
		 * TODO(b/325114016): Use ERROR_STATUS result to adjust the
		 * number of resend attempts. If the command being sent is
		 * either a SET_UOR or SET_PDR, then should have a lower (if
		 * any) number of resend attempts.
		 */
		if (port->send_cmd.resend_counter < CMD_RESEND_MAX) {
			/* Try to resend command */
			if (send_pdc_cmd(port) != 0) {
				/*
				 * Set CCI_ERROR flag to trigger a resend of
				 * the pending command
				 */
				atomic_set_bit(port->cci_flags, CCI_ERROR);
			} else {
				/* PDC command resent, restart wait counter */
				port->send_cmd.wait_counter = 0;
				port->send_cmd.resend_counter++;
			}
		} else {
			LOG_ERR("%s resend attempts exceeded!",
				pdc_cmd_names[port->cmd->cmd]);
			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
	} else if (atomic_test_and_clear_bit(port->cci_flags,
					     CCI_CMD_COMPLETED)) {
		LOG_DBG("CCI_CMD_COMPLETED");
		if (port->cmd->cmd == CMD_PDC_GET_CONNECTOR_STATUS) {
			handle_connector_status(port);
			return;
		} else {
			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
	}

	port->send_cmd.wait_counter++;
	if (port->send_cmd.wait_counter > WAIT_MAX) {
		port->cmd->error = true;
		if (port->cmd->cmd == CMD_PDC_GET_CONNECTOR_STATUS) {
			/* Can't get connector status. Enter unattached state
			 * with error flag set, so it can reset the PDC */
			port->cmd->cmd = CMD_PDC_RESET;
			set_pdc_state(port, PDC_UNATTACHED);
			return;
		} else {
			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
	}
}

static void pdc_send_cmd_wait_exit(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* Completed with error. Clear complete bit */
	atomic_clear_bit(port->cci_flags, CCI_CMD_COMPLETED);
	port->cmd->pending = false;
	port->snk_policy.pdo_count = 0;

	switch (port->cmd->cmd) {
	case CMD_PDC_GET_PDOS:
		/* Filter out Augmented Power Data Objects (APDO). APDOs come
		 * after the regular PDOS, so it's safe to exclude them from the
		 * pdo_count. */
		/* TODO This is temporary until APDOs can be handled  */
		for (int i = 0; i < PDO_NUM; i++) {
			if (port->snk_policy.pdos[i] & PDO_TYPE_AUGMENTED) {
				port->snk_policy.pdos[i] = 0;
			} else {
				port->snk_policy.pdo_count++;
			}
		}
		break;
	default:
		break;
	}
}

static void pdc_src_snk_typec_only_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);
}

static void pdc_src_snk_typec_only_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	set_attached_flag(port, SNK_SRC_ATTACHED_TYPEC_ONLY_FLAG);

	/* The CCI_EVENT is set on a connector disconnect, so check the
	 * connector status and take the appropriate action. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}
}

static void pdc_init_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	/* Initialize Send Command data */
	send_cmd_init(port);
}

static void pdc_init_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *const config = port->dev->config;

	/* Wait until PDC driver is initialized */
	if (pdc_is_init_done(port->pdc)) {
		LOG_INF("C%d: PDC Subsystem Started", config->connector_num);
		/* Send the connector status command to determine which state to
		 * enter
		 */
		port->send_cmd.intern.cmd = CMD_PDC_GET_CONNECTOR_STATUS;
		port->send_cmd.intern.pending = true;
		set_pdc_state(port, PDC_SEND_CMD_START);
		return;
	}
}

/**
 * @brief Populate state table
 */
static const struct smf_state pdc_states[] = {
	/* Normal States */
	[PDC_INIT] = SMF_CREATE_STATE(pdc_init_entry, pdc_init_run, NULL, NULL),
	[PDC_UNATTACHED] = SMF_CREATE_STATE(pdc_unattached_entry,
					    pdc_unattached_run, NULL, NULL),
	[PDC_SNK_ATTACHED] = SMF_CREATE_STATE(pdc_snk_attached_entry,
					      pdc_snk_attached_run, NULL, NULL),
	[PDC_SRC_ATTACHED] = SMF_CREATE_STATE(pdc_src_attached_entry,
					      pdc_src_attached_run, NULL, NULL),
	[PDC_SEND_CMD_START] = SMF_CREATE_STATE(
		pdc_send_cmd_start_entry, pdc_send_cmd_start_run, NULL, NULL),
	[PDC_SEND_CMD_WAIT] = SMF_CREATE_STATE(pdc_send_cmd_wait_entry,
					       pdc_send_cmd_wait_run,
					       pdc_send_cmd_wait_exit, NULL),
	[PDC_SRC_SNK_TYPEC_ONLY] =
		SMF_CREATE_STATE(pdc_src_snk_typec_only_entry,
				 pdc_src_snk_typec_only_run, NULL, NULL),
};

/**
 * @brief CCI event handler call back
 */
static void pdc_cci_handler_cb(union cci_event_t cci_event, void *cb_data)
{
	struct pdc_port_t *port = (struct pdc_port_t *)cb_data;

	/* Handle busy event from driver */
	if (cci_event.busy) {
		atomic_set_bit(port->cci_flags, CCI_BUSY);
	}

	/* Handle error event from driver */
	if (cci_event.error) {
		atomic_set_bit(port->cci_flags, CCI_ERROR);
	}

	/* Handle command completed event from driver */
	if (cci_event.command_completed) {
		atomic_set_bit(port->cci_flags, CCI_CMD_COMPLETED);
	}

	/* Handle generic vendor defined event from driver */
	if (cci_event.vendor_defined_indicator) {
		atomic_set_bit(port->cci_flags, CCI_EVENT);
	}
}

/**
 * @brief Initialize the PDC Subsystem
 */
static int pdc_subsys_init(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	struct pdc_port_t *port = &data->port;
	const struct pdc_config_t *const config = dev->config;

	/* Make sure PD Controller is ready */
	if (!device_is_ready(port->pdc)) {
		LOG_ERR("PDC not ready");
		return -ENODEV;
	}

	/* This also seeds the Charge Manager */
	invalidate_charger_settings(port);

	/* Init port variables */

	atomic_clear(port->pdc_cmd_flags);
	atomic_clear(port->cci_flags);

	/* Can charge from port by default */
	port->active_charge = true;
	/* Set cci call back */
	pdc_set_handler_cb(port->pdc, pdc_cci_handler_cb, (void *)port);

	/* Initialize state machine run event */
	k_event_init(&port->sm_event);

	/* Initialize command mutex */
	k_mutex_init(&port->mtx);
	smf_set_initial(&port->ctx, &pdc_states[PDC_INIT]);

	/* Create the thread for this port */
	config->create_thread(dev);

	return 0;
}

/**
 * @brief Returns true if command can be executed without a port partner
 * connection
 */
static bool is_connectionless_cmd(enum pdc_cmd_t pdc_cmd)
{
	switch (pdc_cmd) {
	case CMD_PDC_RESET:
		__fallthrough;
	case CMD_PDC_GET_INFO:
		return true;
	default:
		return false;
	}
}

/**
 * @brief Called from a public API function to block until the command completes
 * or time outs
 */
static int public_api_block(int port, enum pdc_cmd_t pdc_cmd)
{
	if (queue_public_cmd(&pdc_data[port]->port, pdc_cmd)) {
		return -EBUSY;
	}

	/* Reset block counter */
	pdc_data[port]->port.block_counter = 0;

	/* TODO: Investigate using a semaphore here instead of while loop */
	/* Block calling thread until command is processed, errors or timeout
	 * occurs. */
	while (pdc_data[port]->port.send_cmd.public.pending &&
	       !pdc_data[port]->port.send_cmd.public.error) {
		/* block until command completes or max block count is reached
		 */

		/* give time for command to be processed */
		k_sleep(K_MSEC(LOOP_DELAY_MS));
		pdc_data[port]->port.block_counter++;
		/*
		 * TODO(b/325070749): This timeout value likely needs to be
		 * adjusted given that internal commands may be resent up to 2
		 * times with a 2 second timeout for each send attempt.
		 */
		if (pdc_data[port]->port.block_counter > WAIT_MAX) {
			/* something went wrong */
			LOG_ERR("Public API blocking timeout");
			return -EBUSY;
		}

		/* Check for commands that don't require a connection */
		if (is_connectionless_cmd(pdc_cmd)) {
			continue;
		}

		/* The system is blocking on a command that requires a
		 * connection, so return if disconnected */
		if (!pdc_power_mgmt_is_connected(port)) {
			return -EIO;
		}
	}

	if (pdc_data[port]->port.send_cmd.public.error) {
		LOG_ERR("Public API command not sent");
		return -EIO;
	}

	return 0;
}

bool is_pdc_port_valid(int port)
{
	return (port >= 0) && (port < CONFIG_USB_PD_PORT_MAX_COUNT);
}

/**
 * PDC Power Management Public API
 */
static bool pdc_power_mgmt_is_sink_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_snk;
}

static bool pdc_power_mgmt_is_source_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_src;
}

static bool pdc_power_mgmt_is_typec_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_snk_src_typec_only;
}

bool pdc_power_mgmt_is_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_src ||
	       pdc_data[port]->port.attached_snk ||
	       pdc_data[port]->port.attached_snk_src_typec_only;
}

uint8_t pdc_power_mgmt_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

int pdc_power_mgmt_set_active_charge_port(int charge_port)
{
	if (!is_pdc_port_valid(charge_port)) {
		return 1;
	}

	/* TODO: b/319712834 - Brox: validate charging from multiple ports */
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == charge_port) {
			pdc_data[i]->port.active_charge = true;
		} else {
			pdc_data[i]->port.active_charge = false;
		}
	}

	return EC_SUCCESS;
}

void pdc_power_mgmt_set_new_power_request(int port)
{
	/* Make sure port is sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return;
	}

	atomic_set_bit(pdc_data[port]->port.snk_policy.flags,
		       SNK_POLICY_NEW_POWER_REQUEST);
}

uint8_t pdc_power_mgmt_get_task_state(int port)
{
	/* TODO */
	return 0;
}

int pdc_power_mgmt_comm_is_enabled(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	/* TODO */
	return true;
}

bool pdc_power_mgmt_get_vconn_state(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	/* TODO: Add driver support for this */
	return true;
}

bool pdc_power_mgmt_get_partner_usb_comm_capable(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	return pdc_data[port]->port.ccaps.op_mode_usb2 |
	       pdc_data[port]->port.ccaps.op_mode_usb3 |
	       pdc_data[port]->port.ccaps.ext_op_mode_usb4_gen2 |
	       pdc_data[port]->port.ccaps.ext_op_mode_usb4_gen3 |
	       pdc_data[port]->port.ccaps.ext_op_mode_usb4_gen4;
}

bool pdc_power_mgmt_get_partner_unconstr_power(int port)
{
	/* Make sure port is sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return false;
	}

	return (pdc_data[port]->port.snk_policy.pdo &
		PDO_FIXED_GET_UNCONSTRAINED_PWR);
}

int pdc_power_mgmt_accept_data_swap(int port, bool val)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 1;
	}

	/* Set DR accept swap policy */
	pdc_data[port]->port.uor.accept_dr_swap = val;

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_SET_UOR)) {
		/* something went wrong */
		return 1;
	}

	return 0;
}

int pdc_power_mgmt_accept_power_swap(int port, bool val)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 1;
	}

	/* Set PR accept swap policy */
	pdc_data[port]->port.pdr.accept_pr_swap = val;

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_SET_PDR)) {
		/* something went wrong */
		return 1;
	}

	return EC_SUCCESS;
}

static int pdc_power_mgmt_request_data_swap_intern(int port,
						   enum pd_data_role role)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 1;
	}

	/* Set DR accept swap policy */
	if (role == PD_ROLE_UFP) {
		/* Attempt to swapt to UFP */
		pdc_data[port]->port.uor.swap_to_dfp = 0;
		pdc_data[port]->port.uor.swap_to_ufp = 1;
	} else if (role == PD_ROLE_DFP) {
		/* Attempt to swapt to DFP */
		pdc_data[port]->port.uor.swap_to_dfp = 1;
		pdc_data[port]->port.uor.swap_to_ufp = 0;
	} else {
		return EC_SUCCESS;
	}

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_SET_UOR)) {
		/* something went wrong */
		return 1;
	}

	return EC_SUCCESS;
}

void pdc_power_mgmt_request_data_swap_to_ufp(int port)
{
	pdc_power_mgmt_request_data_swap_intern(port, PD_ROLE_UFP);
}

void pdc_power_mgmt_request_data_swap_to_dfp(int port)
{
	pdc_power_mgmt_request_data_swap_intern(port, PD_ROLE_DFP);
}

void pdc_power_mgmt_request_data_swap(int port)
{
	if (pdc_power_mgmt_pd_get_data_role(port) == PD_ROLE_DFP) {
		pdc_power_mgmt_request_data_swap_intern(port, PD_ROLE_UFP);
	} else if (pdc_power_mgmt_pd_get_data_role(port) == PD_ROLE_UFP) {
		pdc_power_mgmt_request_data_swap_intern(port, PD_ROLE_DFP);
	}
}

static int pdc_power_mgmt_request_power_swap_intern(int port,
						    enum pd_power_role role)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 1;
	}

	/* Set PR accept swap policy */
	if (role == PD_ROLE_SOURCE) {
		/* Attempt to swap to SOURCE */
		pdc_data[port]->port.pdr.swap_to_snk = 0;
		pdc_data[port]->port.pdr.swap_to_src = 1;
	} else {
		/* Attempt to swap to SINK */
		pdc_data[port]->port.pdr.swap_to_snk = 1;
		pdc_data[port]->port.pdr.swap_to_src = 0;
	}

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_SET_PDR)) {
		/* something went wrong */
		return 1;
	}

	return EC_SUCCESS;
}

void pdc_power_mgmt_request_swap_to_src(int port)
{
	pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SOURCE);
}

void pdc_power_mgmt_request_swap_to_snk(int port)
{
	pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SINK);
}

void pdc_power_mgmt_request_power_swap(int port)
{
	if (pdc_power_mgmt_is_sink_connected(port)) {
		pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SOURCE);
	} else if (pdc_power_mgmt_is_source_connected(port)) {
		pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SINK);
	}
}

enum tcpc_cc_polarity pdc_power_mgmt_pd_get_polarity(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return POLARITY_COUNT;
	}

	if (pdc_data[port]->port.connector_status.orientation) {
		return POLARITY_CC2;
	}

	return POLARITY_CC1;
}

enum pd_data_role pdc_power_mgmt_pd_get_data_role(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return PD_ROLE_DISCONNECTED;
	}

	if (pdc_data[port]->port.connector_status.conn_partner_type ==
	    DFP_ATTACHED) {
		return PD_ROLE_UFP;
	}

	return PD_ROLE_DFP;
}

enum pd_power_role pdc_power_mgmt_get_power_role(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return PD_ROLE_SINK;
	}

	if (pdc_data[port]->port.connector_status.power_direction) {
		return PD_ROLE_SOURCE;
	}

	return PD_ROLE_SINK;
}

enum pd_cc_states pdc_power_mgmt_get_task_cc_state(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return PD_CC_NONE;
	}

	switch (pdc_data[port]->port.connector_status.conn_partner_type) {
	case DFP_ATTACHED:
		return PD_CC_DFP_ATTACHED;
	case UFP_ATTACHED:
		return PD_CC_UFP_ATTACHED;
	case POWERED_CABLE_NO_UFP_ATTACHED:
		return PD_CC_NONE;
	case POWERED_CABLE_UFP_ATTACHED:
		return PD_CC_UFP_ATTACHED;
	case DEBUG_ACCESSORY_ATTACHED:
		return PD_CC_UFP_DEBUG_ACC;
	case AUDIO_ADAPTER_ACCESSORY_ATTACHED:
		return PD_CC_UFP_AUDIO_ACC;
	}

	return PD_CC_NONE;
}

bool pdc_power_mgmt_pd_capable(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	return pdc_data[port]->port.pd_capable;
}

bool pdc_power_mgmt_get_partner_dual_role_power(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	return pdc_data[port]->port.ccaps.op_mode_drp;
}

bool pdc_power_mgmt_get_partner_data_swap_capable(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	/* Make sure port partner is DRP, RP only, or RD only */
	if (!pdc_data[port]->port.ccaps.op_mode_drp &&
	    !pdc_data[port]->port.ccaps.op_mode_rp_only &&
	    !pdc_data[port]->port.ccaps.op_mode_rd_only) {
		return false;
	}

	/* Return swap to UFP or DFP capability */
	return pdc_data[port]->port.ccaps.swap_to_dfp ||
	       pdc_data[port]->port.ccaps.swap_to_ufp;
}

uint32_t pdc_power_mgmt_get_vbus_voltage(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 0;
	}

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_GET_VBUS_VOLTAGE)) {
		/* something went wrong */
		return 0;
	}

	/* Return VBUS */
	return pdc_data[port]->port.vbus;
}

void pdc_power_mgmt_reset(int port)
{
	/* Block until command completes */
	public_api_block(port, CMD_PDC_RESET);
}

uint8_t pdc_power_mgmt_get_src_cap_cnt(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return 0;
	}

	return pdc_data[port]->port.snk_policy.pdo_count;
}

const uint32_t *const pdc_power_mgmt_get_src_caps(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return NULL;
	}

	return (const uint32_t *const)pdc_data[port]->port.snk_policy.pdos;
}

const char *pdc_power_mgmt_get_task_state_name(int port)
{
	if (pdc_power_mgmt_is_typec_connected(port)) {
		return pdc_state_names[PDC_SRC_SNK_TYPEC_ONLY];
	} else if (pdc_power_mgmt_is_sink_connected(port)) {
		return pdc_state_names[PDC_SNK_ATTACHED];
	} else if (pdc_power_mgmt_is_source_connected(port)) {
		return pdc_state_names[PDC_SRC_ATTACHED];
	}

	return pdc_state_names[PDC_UNATTACHED];
}

void pdc_power_mgmt_set_dual_role(int port, enum pd_dual_role_states state)
{
	switch (state) {
	/* While disconnected, toggle between src and sink */
	case PD_DRP_TOGGLE_ON:
		pdc_data[port]->port.una_policy.cc_mode = CCOM_DRP;
		atomic_set_bit(pdc_data[port]->port.una_policy.flags,
			       UNA_POLICY_CC_MODE);
		break;
	/* Stay in src until disconnect, then stay in sink forever */
	case PD_DRP_TOGGLE_OFF:
		pdc_data[port]->port.una_policy.cc_mode = CCOM_RD;
		atomic_set_bit(pdc_data[port]->port.una_policy.flags,
			       UNA_POLICY_CC_MODE);
		break;
	/* Stay in current power role, don't switch. No auto-toggle support */
	case PD_DRP_FREEZE:
		if (pdc_power_mgmt_is_source_connected(port)) {
			pdc_data[port]->port.una_policy.cc_mode = CCOM_RP;
		} else {
			pdc_data[port]->port.una_policy.cc_mode = CCOM_RD;
		}
		atomic_set_bit(pdc_data[port]->port.una_policy.flags,
			       UNA_POLICY_CC_MODE);
		break;
	/* Switch to sink */
	case PD_DRP_FORCE_SINK:
		if (pdc_power_mgmt_is_source_connected(port)) {
			pdc_data[port]->port.pdr.swap_to_src = 0;
			pdc_data[port]->port.pdr.swap_to_snk = 1;
			atomic_set_bit(pdc_data[port]->port.src_policy.flags,
				       SRC_POLICY_SWAP_TO_SNK);
		}
		break;
	/* Switch to source */
	case PD_DRP_FORCE_SOURCE:
		if (pdc_power_mgmt_is_sink_connected(port)) {
			pdc_data[port]->port.pdr.swap_to_src = 1;
			pdc_data[port]->port.pdr.swap_to_snk = 0;
			atomic_set_bit(pdc_data[port]->port.snk_policy.flags,
				       SNK_POLICY_SWAP_TO_SRC);
		}
		break;
	}
}

/**
 * PDC Chipset state Policies
 */

/**
 * @brief Chipset Resume (S3->S0) Policy 1: Power Role Swap to Source if:
 *	a) Port is attached as a Sink
 *	b) No source caps were received from the port partner
 *	c) Port Partner PDO is Unconstrained Power or NOT DRP
 *	d) Port isn't a charging port
 */
static void enforce_pd_chipset_resume_policy_1(int port)
{
	LOG_DBG("Chipset Resume Policy 1");

	/* a) Port is attached as a Sink */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return;
	}

	/* b) No source caps were received from the port partner */
	if (pdc_data[port]->port.snk_policy.pdo_count == 0) {
		return;
	}

	/* c) Unconstrained Power or NOT Dual Role Power we can charge from */
	if (!(pdc_data[port]->port.snk_policy.pdo &
	      PDO_FIXED_GET_UNCONSTRAINED_PWR) &&
	    (pdc_data[port]->port.snk_policy.pdo & PDO_FIXED_DUAL_ROLE)) {
		return;
	}

	/* d) Port isn't a charging port */
	if (charge_manager_get_active_charge_port() == port) {
		return;
	}

	pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SOURCE);
}

/**
 * @brief Chipset Resume (S3->S0) Policy 2:
 *	a) DRP Toggle ON
 */
static void enforce_pd_chipset_resume_policy_2(int port)
{
	LOG_DBG("C%d: Chipset Resume Policy 2", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_ON);
}

static void pd_chipset_resume(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		enforce_pd_chipset_resume_policy_1(i);
		enforce_pd_chipset_resume_policy_2(i);
	}

	LOG_INF("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

/**
 * @brief Chipset Suspend (S0->S3) Policy 1:
 *	a) DRP TOGGLE OFF
 */
static void enforce_pd_chipset_suspend_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Suspend Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_OFF);
}

static void pd_chipset_suspend(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		enforce_pd_chipset_suspend_policy_1(i);
	}

	LOG_INF("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

/**
 * @brief Chipset Startup (S5->S3) Policy 1:
 *	a) DRP Toggle OFF
 */
static void enforce_pd_chipset_startup_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Startup Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_OFF);
}

static void pd_chipset_startup(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		enforce_pd_chipset_startup_policy_1(i);
	}

	LOG_INF("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

/**
 * Chipset Shutdown (S3->S5) Policy 1:
 *	a) DRP Force SINK
 */
static void enforce_pd_chipset_shutdown_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Shutdown Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_FORCE_SINK);
}

static void pd_chipset_shutdown(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		enforce_pd_chipset_shutdown_policy_1(i);
	}

	LOG_INF("PD:S3->S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pd_chipset_shutdown, HOOK_PRIO_DEFAULT);

int pdc_power_mgmt_get_info(int port, struct pdc_info_t *pdc_info)
{
	int ret;

	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (pdc_info == NULL) {
		return -EINVAL;
	}

	/* Block until command completes */
	ret = public_api_block(port, CMD_PDC_GET_INFO);
	if (ret) {
		return ret;
	}

	/* Provide a copy of the current info struct to avoid exposing internal
	 * data structs.
	 */

	memcpy(pdc_info, &pdc_data[port]->port.info, sizeof(struct pdc_info_t));
	return 0;
}

int pdc_power_mgmt_get_bus_info(int port, struct pdc_bus_info_t *pdc_bus_info)
{
	/* This operation is handled synchronously within the driver based on
	 * compile-time data. No need to block or go through the state machine.
	 */

	return pdc_get_bus_info(pdc_data[port]->port.pdc, pdc_bus_info);
}
