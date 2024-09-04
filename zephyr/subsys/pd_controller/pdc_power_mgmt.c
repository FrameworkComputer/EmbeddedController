/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * PD Controller subsystem
 */

#define DT_DRV_COMPAT named_usbc_port

#include "charge_manager.h"
#include "chipset.h"
#include "hooks.h"
#include "test/util.h"
#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys_clock.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#include <drivers/pdc.h>
#include <usbc/utils.h>

LOG_MODULE_REGISTER(pdc_power_mgmt, CONFIG_USB_PDC_LOG_LEVEL);

/**
 * @brief Event triggered by sending an internal command
 */
#define PDC_SM_EVENT BIT(0)

/**
 * @brief Event triggered when a public command has completed
 */
#define PDC_PUBLIC_CMD_COMPLETE_EVENT BIT(1)

/**
 * @brief Event triggered when pdc state has settled
 */
#define PDC_SM_SETTLED_EVENT BIT(2)

/**
 * @brief Time delay before running the state machine loop
 */
#define LOOP_DELAY_MS 25

/**
 * @brief Time delay to wait for a public command to complete
 */
#define PUBLIC_CMD_DELAY_MS 10

/**
 * @brief Maximum time to wait for a command to complete.
 */
#define PDC_CMD_TIMEOUT_MS 2000

/**
 * @brief maximum number of times to try and send a command, or wait for a
 * public API command to execute (Time is 2s)
 *
 */
#define WAIT_MAX (PDC_CMD_TIMEOUT_MS / LOOP_DELAY_MS)

/**
 * @brief Maximum time to wait for PDC state to settle.
 */
/* TODO(b/362781605): Improve TI driver response time */
#define PDC_SM_SETTLED_TIMEOUT_MS (PDC_CMD_TIMEOUT_MS * 10)

/** @brief Delay to wait for stable power state before running hooks */
#define PDC_POWER_STATE_DEBOUNCE_S (K_SECONDS(2))

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
 * @brief maximum number of VDOs
 */
#define VDO_NUM 8

/**
 * @brief Cached duration for VBUS voltage.
 */
#define VBUS_READ_CACHE_MS 500

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
	/** CMD_PDC_SET_DRP */
	CMD_PDC_SET_DRP,
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
	/** CMD_PDC_GET_CABLE_PROPERTY */
	CMD_PDC_GET_CABLE_PROPERTY,
	/** CMD_PDC_GET_VDO */
	CMD_PDC_GET_VDO,
	/** CMD_PDC_CONNECTOR_RESET */
	CMD_PDC_CONNECTOR_RESET,
	/** CMD_PDC_GET_IDENTITY_DISCOVERY */
	CMD_PDC_GET_IDENTITY_DISCOVERY,
	/** CMD_PDC_IS_SOURCING_VCONN */
	CMD_PDC_IS_VCONN_SOURCING,
	/** CMD_PDC_GET_PD_VDO_DP_CFG */
	CMD_PDC_GET_PD_VDO_DP_CFG_SELF,
	/** CMD_PDC_SET_PDOS */
	CMD_PDC_SET_PDOS,
	/** CMD_PDC_GET_PCH_DATA_STATUS */
	CMD_PDC_GET_PCH_DATA_STATUS,
	/** CMD_PDC_ACK_CC_CI */
	CMD_PDC_ACK_CC_CI,
	/** CMD_PDC_GET_LPM_PPM_INFO */
	CMD_PDC_GET_LPM_PPM_INFO,
	/** CMD_PDC_GET_PD_VDO_DP_STATUS */
	CMD_PDC_GET_PD_VDO_DP_STATUS,
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
	/** SNK_ATTACHED_GET_CABLE_PROPERTY */
	SNK_ATTACHED_GET_CABLE_PROPERTY,
	/** SNK_ATTACHED_SET_DR_SWAP_POLICY */
	SNK_ATTACHED_SET_DR_SWAP_POLICY,
	/** SNK_ATTACHED_SET_PR_SWAP_POLICY */
	SNK_ATTACHED_SET_PR_SWAP_POLICY,
	/** SNK_ATTACHED_GET_PDOS */
	SNK_ATTACHED_GET_PDOS,
	/** SNK_ATTACHED_GET_VDO */
	SNK_ATTACHED_GET_VDO,
	/** SNK_ATTACHED_GET_RDO */
	SNK_ATTACHED_GET_RDO,
	/** SNK_ATTACHED_SET_SINK_PATH */
	SNK_ATTACHED_SET_SINK_PATH,
	/** SNK_ATTACHED_EVALUATE_PDOS */
	SNK_ATTACHED_EVALUATE_PDOS,
	/** SNK_ATTACHED_START_CHARGING */
	SNK_ATTACHED_START_CHARGING,
	/** SNK_ATTACHED_RUN */
	SNK_ATTACHED_RUN,
};

/**
 * @brief SRC Attached Local States
 */
enum src_attached_local_state_t {
	/** SRC_ATTACHED_SET_SINK_PATH_OFF */
	SRC_ATTACHED_SET_SINK_PATH_OFF,
	/** SRC_ATTACHED_GET_CONNECTOR_CAPABILITY */
	SRC_ATTACHED_GET_CONNECTOR_CAPABILITY,
	/** SRC_ATTACHED_GET_CABLE_PROPERTY */
	SRC_ATTACHED_GET_CABLE_PROPERTY,
	/** SRC_ATTACHED_SET_DR_SWAP_POLICY */
	SRC_ATTACHED_SET_DR_SWAP_POLICY,
	/** SRC_ATTACHED_SET_PR_SWAP_POLICY */
	SRC_ATTACHED_SET_PR_SWAP_POLICY,
	/** SRC_ATTACHED_READ_POWER_LEVEL */
	SRC_ATTACHED_READ_POWER_LEVEL,
	/** SRC_ATTACHED_GET_VDO */
	SRC_ATTACHED_GET_VDO,
	/** SRC_ATTACHED_GET_PDOS */
	SRC_ATTACHED_GET_PDOS,
	/** SRC_ATTACHED_RUN */
	SRC_ATTACHED_RUN,
};

/**
 * @brief TypeC SNK Attached Local States
 */
enum snk_typec_attached_local_state_t {
	/** SNK_TYPEC_ATTACHED_SET_CHARGE_CURRENT */
	SNK_TYPEC_ATTACHED_SET_CHARGE_CURRENT,
	/** SNK_TYPEC_ATTACHED_SET_SINK_PATH_ON */
	SNK_TYPEC_ATTACHED_SET_SINK_PATH_ON,
	/** SNK_TYPEC_ATTACHED_DEBOUNCE */
	SNK_TYPEC_ATTACHED_DEBOUNCE,
	/** SNK_TYPEC_ATTACHED_RUN */
	SNK_TYPEC_ATTACHED_RUN,
};

/**
 * @brief TypeC SRC Attached Local States
 */
enum src_typec_attached_local_state_t {
	/** SRC_TYPEC_ATTACHED_SET_SINK_PATH_OFF */
	SRC_TYPEC_ATTACHED_SET_SINK_PATH_OFF,
	/** SRC_TYPEC_ATTACHED_DEBOUNCE */
	SRC_TYPEC_ATTACHED_DEBOUNCE,
	/** SRC_TYPEC_ATTACHED_ADD_SINK */
	SRC_TYPEC_ATTACHED_ADD_SINK,
	/** SRC_TYPEC_ATTACHED_RUN */
	SRC_TYPEC_ATTACHED_RUN,
};

/**
 * @brief Unattached Local States
 */
enum unattached_local_state_t {
	/** UNATTACHED_SET_SINK_PATH_OFF */
	UNATTACHED_SET_SINK_PATH_OFF,
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
	/** CCI_EVENT: Used to trigger querying connector status */
	CCI_EVENT,
	/** CCI_CAM_CHANGE */
	CCI_CAM_CHANGE,
	/** CCI_ACK */
	CCI_ACK,
	/** CCI_ATTENTION */
	CCI_ATTENTION,
	/** CCI_PPM_EVENT */
	CCI_PPM_EVENT,
	/** CCI_FLAGS_COUNT */
	CCI_FLAGS_COUNT
};

/**
 * @brief PDC Command Names
 */
test_export_static const char *const pdc_cmd_names[] = {
	[CMD_PDC_NONE] = "",
	[CMD_PDC_RESET] = "PDC_RESET",
	[CMD_PDC_SET_POWER_LEVEL] = "PDC_SET_POWER_LEVEL",
	[CMD_PDC_SET_CCOM] = "PDC_SET_CCOM",
	[CMD_PDC_SET_DRP] = "PDC_SET_DRP",
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
	[CMD_PDC_GET_CABLE_PROPERTY] = "PDC_GET_CABLE_PROPERTY",
	[CMD_PDC_GET_VDO] = "PDC_GET_VDO",
	[CMD_PDC_CONNECTOR_RESET] = "PDC_CONNECTOR_RESET",
	[CMD_PDC_GET_IDENTITY_DISCOVERY] = "PDC_GET_IDENTITY_DISCOVERY",
	[CMD_PDC_IS_VCONN_SOURCING] = "PDC_IS_VCONN_SOURCING",
	[CMD_PDC_GET_PD_VDO_DP_CFG_SELF] = "PDC_GET_PD_VDO_DP_CFG_SELF",
	[CMD_PDC_SET_PDOS] = "PDC_SET_PDOS",
	[CMD_PDC_GET_PCH_DATA_STATUS] = "PDC_GET_PCH_DATA_STATUS",
	[CMD_PDC_ACK_CC_CI] = "PDC_ACK_CC_CI",
	[CMD_PDC_GET_LPM_PPM_INFO] = "PDC_GET_LPM_PPM_INFO",
	[CMD_PDC_GET_PD_VDO_DP_STATUS] = "PDC_GET_PD_VDO_DP_STATUS",
};
const int pdc_cmd_types = CMD_PDC_COUNT;

BUILD_ASSERT(ARRAY_SIZE(pdc_cmd_names) == CMD_PDC_COUNT);

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
	[PDC_SRC_TYPEC_ONLY] = "TypeCSrcAttached",
	[PDC_SNK_TYPEC_ONLY] = "TypeCSnkAttached",
	[PDC_SUSPENDED] = "Suspended",
};

BUILD_ASSERT(ARRAY_SIZE(pdc_state_names) == PDC_STATE_COUNT,
	     "pdc_state_names array has wrong number of elements");

/**
 * @brief Unattached policy flags
 */
enum policy_unattached_t {
	/** UNA_POLICY_TCC */
	UNA_POLICY_TCC,
	/** UNA_POLICY_CC_MODE */
	UNA_POLICY_CC_MODE,
	/** UNA_POLICY_UPDATE_SRC_CAPS */
	UNA_POLICY_UPDATE_SRC_CAPS,
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
	/** Selects the active charge port */
	SNK_POLICY_SET_ACTIVE_CHARGE_PORT,
	/** Runs a test to determine if we should become a source instead */
	SNK_POLICY_EVAL_SWAP_TO_SRC,
	/** Triggers an update of the allow_pr_swap bit in CMD_SET_DRP */
	SNK_POLICY_UPDATE_ALLOW_PR_SWAP,
	/** Sends SET_PDO to the LPM. */
	SNK_POLICY_UPDATE_SRC_CAPS,
	/** SNK_POLICY_COUNT */
	SNK_POLICY_COUNT,
};

/**
 * @brief Attached state
 */
enum attached_state_t {
	/** UNATTACHED_STATE */
	UNATTACHED_STATE,
	/** SRC_ATTACHED_STATE */
	SRC_ATTACHED_STATE,
	/** SNK_ATTACHED_STATE */
	SNK_ATTACHED_STATE,
	/** SRC_ATTACHED_TYPEC_ONLY_STATE */
	SRC_ATTACHED_TYPEC_ONLY_STATE,
	/** SNK_ATTACHED_TYPEC_ONLY_STATE */
	SNK_ATTACHED_TYPEC_ONLY_STATE,
};

static const char *const attached_state_names[] = {
	[UNATTACHED_STATE] = "Unattached",
	[SRC_ATTACHED_STATE] = "Attached.SRC",
	[SNK_ATTACHED_STATE] = "Attached.SNK",
	[SRC_ATTACHED_TYPEC_ONLY_STATE] = "TypeCSrcAttached",
	[SNK_ATTACHED_TYPEC_ONLY_STATE] = "TypeCSnkAttached",
};

/**
 * @brief Common struct for PDOs
 */
struct pdc_pdos_t {
	/** PDOs */
	uint32_t pdos[PDO_NUM];
	/** PDO count */
	uint8_t pdo_count;
};

/**
 * @brief Struct for SET_PDOS command
 */
struct set_pdos_t {
	/** PDOs for SRC or SNK CAPs */
	uint32_t pdos[PDO_NUM];
	/** PDO count */
	uint8_t count;
	/** SRC or SNK pdo */
	enum pdo_type_t type;
};

/**
 * @brief Sink attached policy object
 */
struct pdc_snk_attached_policy_t {
	/** SNK Attached policy flags */
	ATOMIC_DEFINE(flags, SNK_POLICY_COUNT);
	/** Currently active PDO */
	uint32_t pdo;
	/** Current active PDO index */
	uint32_t pdo_index;
	/** PDO count */
	uint8_t pdo_count;
	/** PDOs for Sink Caps */
	struct pdc_pdos_t snk;
	/** PDOs for Source Caps */
	struct pdc_pdos_t src;
	/** Sent RDO */
	uint32_t rdo;
	/** New RDO to send */
	uint32_t rdo_to_send;
	/** If true, accept a power role swap request from port partner */
	bool accept_power_role_swap;
};

/**
 * @brief Source attached policy flags
 */
enum policy_src_attached_t {
	/** Enables swap to Sink */
	SRC_POLICY_SWAP_TO_SNK,
	/** Forces sink-only operation, even if it requires a disconnect */
	SRC_POLICY_FORCE_SNK,
	/** Triggers sending CMD_SET_POWER_LEVEL to set Rp value */
	SRC_POLICY_SET_RP,
	/** Trigger a call into DPM source current balancing policy */
	SRC_POLICY_EVAL_SNK_FIXED_PDO,
	/** Set new SRC CAP for PDC port in source power role */
	SRC_POLICY_UPDATE_SRC_CAPS,
	/**
	 * Triggers sending CMD_PDC_GET_RDO to extract RDO for current
	 * balancing policy.
	 */
	SRC_POLICY_GET_RDO,
	/** Triggers an update of the allow_pr_swap bit in CMD_SET_DRP */
	SRC_POLICY_UPDATE_ALLOW_PR_SWAP,

	/** SRC_POLICY_COUNT */
	SRC_POLICY_COUNT
};

/**
 * @brief Source attached policy object
 */
struct pdc_src_attached_policy_t {
	/** SRC Attached policy flags */
	ATOMIC_DEFINE(flags, SRC_POLICY_COUNT);
	/** PDOs for Sink caps */
	struct pdc_pdos_t snk;
	/** PDOs for Source caps */
	struct pdc_pdos_t src;
	/** Request RDO from port partner */
	uint32_t rdo;
	/** If true, accept a power role swap request from port partner */
	bool accept_power_role_swap;
};

/**
 * @brief Indices used to map which VDO to use to extract the desired field
 */
#define IDENTITY_VID_VDO_IDX 0
#define IDENTITY_PTYPE_VDO_IDX 0
#define IDENTITY_PID_VDO_IDX 1

/**
 * @brief Invalid value for VDO used to check if VDO has been queried already.
 */
#define INVALID_VDO_VALUE -1u

/**
 * @brief Table of VDO types to request in the GET_VDO command
 */
static const enum vdo_type_t vdo_discovery_list[] = {
	VDO_ID_HEADER,
	VDO_PRODUCT,
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
	/** Flag to suspend the PDC Power Mgmt state machine */
	atomic_t suspend;
	/** Flag to notify that a Hard Reset was sent */
	atomic_t hard_reset_sent;

	/** Source TypeC attached local state variable */
	enum src_typec_attached_local_state_t src_typec_attached_local_state;
	/** Sink TypeC attached local state variable */
	enum snk_typec_attached_local_state_t snk_typec_attached_local_state;
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

	/** Cable Property */
	union cable_property_t cable_prop;
	/** PDC version and other information */
	struct pdc_info_t info;
	/** Command mutex */
	struct k_mutex mtx;
	/** PDC command to send */
	struct send_cmd_t send_cmd;
	/** Pointer to current pending command */
	struct cmd_t *cmd;
	/** Bit mask of port events; see PD_STATUS_EVENT_* */
	atomic_t port_event;
	/** CCAPS temp variable used with CMD_PDC_GET_CONNECTOR_CAPABILITY
	 * command */
	union connector_capability_t ccaps;
	/** CONNECTOR_STATUS temp variable used with CONNECTOR_GET_STATUS
	 * command */
	union connector_status_t connector_status;
	/** SINK_PATH_EN temp variable used with CMD_PDC_SET_SINK_PATH command
	 */
	bool sink_path_en;
	/**
	 * Time at which the current vbus value is expired and should be
	 * re-queried.
	 */
	k_timepoint_t vbus_expired;
	/** VBUS temp variable used with CMD_PDC_GET_VBUS_VOLTAGE command */
	uint16_t vbus;
	/** UOR variable used with CMD_PDC_SET_UOR command */
	union uor_t uor;
	/** PDR variable used with CMD_PDC_SET_PDR command */
	union pdr_t pdr;
	/** True if battery can charge from this port */
	bool active_charge;
	/** Tracks current connection state */
	enum attached_state_t attached_state;
	/** GET_VDO temp variable used with CMD_GET_VDO */
	union get_vdo_t vdo_req;
	/** LPM_PPM_INFO temp variable to hold user buffer pointer */
	struct lpm_ppm_info_t *lpm_ppm_info;
	/** Array used to hold the list of VDO types to request */
	uint8_t vdo_type[VDO_NUM];
	/** Array used to store VDOs returned from the GET_VDO command */
	uint32_t vdo[VDO_NUM];
	/** Store the VDO returned for the PD_VDO_DP_CFG */
	uint32_t vdo_dp_cfg;
	/** Store the VDO returned for the PD_VDO_DP_STATUS */
	uint32_t vdo_dp_status;
	/** CONNECTOR_RESET temp variable used with CMD_PDC_CONNECTOR_RESET */
	union connector_reset_t connector_reset;
	/** PD Port Partner discovery state: True if discovery is complete, else
	 * false */
	bool discovery_state;
	/** Charge current while in TypeC Sink state */
	uint32_t typec_current_ma;
	/** Buffer used by public api to receive data from the driver */
	uint8_t *public_api_buff;
	/** Timer to used to verify typec_only vs USB-PD port partner */
	struct k_timer typec_only_timer;
	/** Type of PDOs to get: SNK|SRC from PDC or Port Partner */
	struct get_pdo_t get_pdo;
	/** Variable used to store/set PDC LPM SRC CAPs */
	struct set_pdos_t set_pdos;
	/** Buffer used by public api to receive data from the driver */
	uint8_t pch_data_status[5];
	/** SET_DRP variable used with CMD_SET_DRP */
	enum drp_mode_t drp;
	/** Callback */
	struct pdc_callback cc_cb;
	struct pdc_callback ci_cb;
	/** Callback for PPM */
	const struct pdc_callback *ppm_ci_cb;
	/** Last configured dual role power state */
	enum pd_dual_role_states dual_role_state;
	/** Change indicator bits to clear */
	union conn_status_change_bits_t ci;
	/** Command complete clear bit */
	bool cc;
	/** Vendor defined change indicator bits */
	uint16_t vendor_defined_ci;
	/** System should watch for an HPD wake */
	bool hpd_wake_watch;
	/** Additional change bits to report to PPM. */
	union conn_status_change_bits_t overlay_ppm_changes;
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

static const uint32_t pdo_fixed_flags =
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP);

static const uint32_t pdc_src_pdo_nominal[] = {
	PDO_FIXED(5000, 1500, pdo_fixed_flags),
};
static const uint32_t pdc_src_pdo_max[] = {
	PDO_FIXED(5000, 3000, pdo_fixed_flags),
};

static const struct smf_state pdc_states[];
static enum pdc_state_t get_pdc_state(struct pdc_port_t *port);
static void set_pdc_state(struct pdc_port_t *port, enum pdc_state_t next_state);
static int pdc_subsys_init(const struct device *dev);
static void send_cmd_init(struct pdc_port_t *port);
static void queue_internal_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd);
static int queue_public_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd);
static void init_port_variables(struct pdc_port_t *port);
static int pdc_power_mgmt_request_power_swap_intern(int port,
						    enum pd_power_role role);
static void pd_chipset_startup(void);
static void pd_chipset_resume(void);
static void pd_chipset_suspend(void);
static void pd_chipset_shutdown(void);

static bool should_suspend(struct pdc_port_t *port)
{
	if (!atomic_get(&port->suspend)) {
		return false;
	}

	/* Suspend has been requested. Wait until we are in a safe state. */

	enum pdc_state_t current_state = get_pdc_state(port);

	switch (current_state) {
	/* Safe states to suspend from */
	case PDC_UNATTACHED:
	case PDC_SNK_ATTACHED:
	case PDC_SRC_ATTACHED:
	case PDC_SNK_TYPEC_ONLY:
	case PDC_SRC_TYPEC_ONLY:
		return true;

	/* Wait for operation to finish. */
	case PDC_INIT:
	case PDC_SEND_CMD_START:
	case PDC_SEND_CMD_WAIT:
		return false;

	/* No need to transition */
	case PDC_SUSPENDED:
		return false;

	case PDC_STATE_COUNT:
		__ASSERT(0, "Invalid state");
	}

	__builtin_unreachable();
}

/**
 * @brief PDC thread
 */
static ALWAYS_INLINE void pdc_thread(void *pdc_dev, void *unused1,
				     void *unused2)
{
	const struct device *dev = (const struct device *)pdc_dev;
	struct pdc_data_t *data = dev->data;
	struct pdc_port_t *port = &data->port;
	int rv;

	while (1) {
		/* Wait for timeout or event */
		rv = k_event_wait(&port->sm_event, PDC_SM_EVENT, false,
				  K_MSEC(LOOP_DELAY_MS));

		/*
		 * If k_event_wait returns a non-zero value, then
		 * always clear PDC_SM_EVENT to ensure that the thread goes to
		 * sleep in cases where PDC_SM_EVENT can't be handled
		 * immediately such as when a public cmd is posted, but is
		 * waiting on an internal cmd to be sent.
		 */
		if (rv != 0) {
			k_event_clear(&port->sm_event, PDC_SM_EVENT);
		}

		if (should_suspend(port)) {
			set_pdc_state(port, PDC_SUSPENDED);
		}

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
		.port.suspend = ATOMIC_INIT(0),                              \
		.port.dual_role_state = PD_DRP_TOGGLE_ON,                    \
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

/**
 * @brief As a sink, this is the max voltage (in millivolts) we can request
 *        before getting source caps
 */
static uint32_t pdc_max_request_mv = CONFIG_PLATFORM_EC_PD_MAX_VOLTAGE_MV;

/**
 * @brief As a sink, this is the max power (in milliwatts) needed to operate
 */
static uint32_t pdc_max_operating_power = CONFIG_PLATFORM_EC_PD_MAX_POWER_MW;

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

static void set_attached_pdc_state(struct pdc_port_t *port,
				   enum attached_state_t attached_state)
{
	const struct pdc_config_t *const config = port->dev->config;

	if (attached_state != port->attached_state) {
		port->attached_state = attached_state;
		LOG_INF("C%d attached: %s", config->connector_num,
			attached_state_names[port->attached_state]);
	}
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
	/* If we are running public commands, policy state machine must have
	 * finished settling.
	 */
	k_event_post(&port->sm_event, PDC_SM_SETTLED_EVENT);

	/* Send a pending public command */
	if (port->send_cmd.public.pending) {
		set_pdc_state(port, PDC_SEND_CMD_START);
	}
}

atomic_val_t pdc_power_mgmt_get_events(int port)
{
	return pdc_data[port]->port.port_event;
}

void pdc_power_mgmt_notify_event(int port, atomic_t event_mask)
{
	atomic_or(&pdc_data[port]->port.port_event, event_mask);
	pd_send_host_event(PD_EVENT_TYPEC);
}

void pdc_power_mgmt_clear_event(int port, atomic_t event_mask)
{
	atomic_and(&pdc_data[port]->port.port_event, ~event_mask);
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
	memset(port->snk_policy.src.pdos, 0, sizeof(uint32_t) * PDO_NUM);
	port->snk_policy.src.pdo_count = 0;
	memset(port->src_policy.snk.pdos, 0, sizeof(uint32_t) * PDO_NUM);
	port->src_policy.snk.pdo_count = 0;
}

/**
 * @brief Callers of this function should return immediately because the PDC
 * state is changed.
 */
static int queue_public_cmd(struct pdc_port_t *port, enum pdc_cmd_t pdc_cmd)
{
	/* Don't send if still in init state */
	enum pdc_state_t s = get_pdc_state(port);

	if (s == PDC_INIT || s == PDC_SUSPENDED) {
		return -ENOTCONN;
	}

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
 * @brief Trigger a PPM change indication on a port.
 */
static void trigger_ppm_ci(struct pdc_port_t *port)
{
	const struct pdc_config_t *config = port->dev->config;
	int port_number = config->connector_num;
	union cci_event_t cci_event;

	if (!port->ppm_ci_cb)
		return;

	cci_event.connector_change = port_number + 1;
	port->ppm_ci_cb->handler(port->dev, port->ppm_ci_cb, cci_event);
}

/**
 * @brief Reads connector status and takes appropriate action.
 *
 * This function should only be called after the completion of the
 * GET_CONNECTOR_STATUS command. It reads the connect_status,
 * power_operation_mode, and power_direction bit to determine which state should
 * be entered.
 * Note: The caller should return after this call if it changed state (returned
 * true).
 *
 * @return true if state changed, false otherwise
 */
static bool handle_connector_status(struct pdc_port_t *port)
{
	union connector_status_t *status = &port->connector_status;
	const struct pdc_config_t *config = port->dev->config;
	int port_number = config->connector_num;
	union conn_status_change_bits_t conn_status_change_bits;

	conn_status_change_bits.raw_value = status->raw_conn_status_change_bits;

	LOG_DBG("C%d: Connector Change: 0x%04x", port_number,
		conn_status_change_bits.raw_value);

	/*
	 * Set CCI_ACK flag to trigger sending ACK_CC_CI to clear the connector
	 * change indicator bits which were just read as part of the connector
	 * status message.
	 */
	if (conn_status_change_bits.raw_value) {
		port->ci.raw_value = conn_status_change_bits.raw_value;
		atomic_set_bit(port->cci_flags, CCI_ACK);
	}

	/* Trigger PPM CI callback if connector status change was indicated. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_PPM_EVENT)) {
		/* We need to also overlay any conn status change bits because
		 * they may disappear by the time OPM reads them.
		 */
		port->overlay_ppm_changes.raw_value |=
			conn_status_change_bits.raw_value;
		trigger_ppm_ci(port);
	}

	if (conn_status_change_bits.pd_reset_complete) {
		LOG_INF("C%d: Reset complete indicator", port_number);
		pdc_power_mgmt_notify_event(port_number,
					    PD_STATUS_EVENT_HARD_RESET);

		atomic_set(&port->hard_reset_sent, true);
	}

	/* On potential power changes, expire the vbus cache immediately. */
	if (conn_status_change_bits.negotiated_power_level ||
	    conn_status_change_bits.connector_partner ||
	    conn_status_change_bits.pwr_direction) {
		port->vbus_expired = sys_timepoint_calc(K_NO_WAIT);
	}

	if (!status->connect_status) {
		/* Port is not connected */
		set_pdc_state(port, PDC_UNATTACHED);
	} else {
		switch (status->power_operation_mode) {
		case USB_DEFAULT_OPERATION:
			port->typec_current_ma = 500;
			break;
		case BC_OPERATION:
			port->typec_current_ma = 500;
			break;
		case PD_OPERATION:
			port->typec_current_ma = 0;
			if (conn_status_change_bits.supported_cam) {
				atomic_set_bit(port->cci_flags, CCI_CAM_CHANGE);
				LOG_INF("C%d: CAM change", port_number);
			}

			if (conn_status_change_bits.attention) {
				atomic_set_bit(port->cci_flags, CCI_ATTENTION);
			}

			if (status->power_direction) {
				/* Port partner is a sink device
				 */
				set_pdc_state(port, PDC_SRC_ATTACHED);
				return true;
			} else {
				/* Port partner is a source
				 * device */
				set_pdc_state(port, PDC_SNK_ATTACHED);
				return true;
			}
			break;
		case USB_TC_CURRENT_1_5A:
			port->typec_current_ma = 1500;
			break;
		case USB_TC_CURRENT_3A:
			port->typec_current_ma = 3000;
			break;
		case USB_TC_CURRENT_5A:
			port->typec_current_ma = 5000;
			break;
		}

		/* TypeC only connection */
		if (status->power_direction) {
			/* Port partner is a Typec Sink device */
			set_pdc_state(port, PDC_SRC_TYPEC_ONLY);
			return true;
		} else {
			/* Port partner is a Typec Source device */
			set_pdc_state(port, PDC_SNK_TYPEC_ONLY);
			return true;
		}
	}

	return true;
}

/**
 * @brief Trigger connector status change on PPM
 *
 * The UCSI spec says that certain commands with side-effects (like SET_PDR) do
 * not generate status change interrupts if the host was the one that caused the
 * change. This can create a state de-sync between the EC and OS so we should
 * fake some connector changes for capture these side effects.
 *
 */
static void trigger_ppm_status_change(struct pdc_port_t *port)
{
	union conn_status_change_bits_t status = { .raw_value = 0 };

	/* No status change on command error. */
	if (!port->cmd || port->cmd->error) {
		return;
	}

	switch (port->cmd->cmd) {
	case CMD_PDC_SET_PDR:
		status.pwr_direction = 1;
		break;
	case CMD_PDC_SET_UOR:
		status.connector_partner = 1;
		break;
	case CMD_PDC_SET_PDOS:
		status.supported_provider_caps = 1;
		break;
	case CMD_PDC_SET_SINK_PATH:
		status.sink_path_status_change = 1;
		break;

	/* For all other commands, no need to trigger as there shouldn't be
	 * side-effects to connector status.
	 */
	default:
		return;
	}

	/* If trigger CI, we should also refresh the connector status. */
	atomic_set_bit(port->cci_flags, CCI_EVENT);
	k_event_post(&port->sm_event, PDC_SM_EVENT);

	port->overlay_ppm_changes.raw_value |= status.raw_value;
	trigger_ppm_ci(port);
}

/**
 * @brief This function is used to format the GET_VDO command which is used to
 * extract VID, PID, and Product Type values from the port partners Discovery
 * Identity response message.
 */
static void discovery_info_init(struct pdc_port_t *port)
{
	int i;

	port->vdo_req.raw_value = 0;
	/* Request VDOs from port partner */
	port->vdo_req.vdo_origin = VDO_ORIGIN_SOP;
	port->vdo_req.num_vdos = ARRAY_SIZE(vdo_discovery_list);

	/* Create the list of VDO types being requested */
	for (i = 0; i < ARRAY_SIZE(vdo_discovery_list); i++) {
		port->vdo_type[i] = vdo_discovery_list[i];
		port->vdo[i] = INVALID_VDO_VALUE;
	}

	/* Clear the DP Config VDO, which stores the DP pin assignment */
	port->vdo_dp_cfg = 0;
	/* Clear DP Status */
	port->vdo_dp_status = 0;
}

/**
 * @brief This function gets the correct pointer for pdc_pdos_t struct
 *
 * These structs are used to store SRC/SNK CAPs PDOs. The correct struct member
 * is determined by the origin (LPM/port partner) and CAP type (SNK/SRC).
 */
static struct pdc_pdos_t *get_pdc_pdos_ptr(struct pdc_port_t *port,
					   struct get_pdo_t *pdo_req)
{
	struct pdc_pdos_t *pdc_pdos;

	if (pdo_req->pdo_source == LPM_PDO && pdo_req->pdo_type == SINK_PDO) {
		pdc_pdos = &port->snk_policy.snk;
	} else if (pdo_req->pdo_source == LPM_PDO &&
		   pdo_req->pdo_type == SOURCE_PDO) {
		pdc_pdos = &port->src_policy.src;
	} else if (pdo_req->pdo_source == PARTNER_PDO &&
		   pdo_req->pdo_type == SINK_PDO) {
		pdc_pdos = &port->src_policy.snk;
	} else {
		pdc_pdos = &port->snk_policy.src;
	}

	return pdc_pdos;
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
		/* Make sure new Rp value is applied */
		atomic_set_bit(port->una_policy.flags, UNA_POLICY_CC_MODE);
		return;
	} else if (atomic_test_and_clear_bit(port->una_policy.flags,
					     UNA_POLICY_UPDATE_SRC_CAPS)) {
		/* Ensure the next time a PD capable SNK connects, we offer
		 * a safe PDO.
		 */
		queue_internal_cmd(port, CMD_PDC_SET_PDOS);
		return;
	}

	send_pending_public_commands(port);
}

static bool should_swap_to_source(struct pdc_port_t *port)
{
	const struct pdc_config_t *config = port->dev->config;
	int port_num = config->connector_num;

	/* If all of the following are true, swap to source:
	 *  a) Source caps were received from the port partner
	 *  b) Port partner supports DRP and does not offer unconstrained power
	 *  c) Port isn't the active charging port.
	 */

	if (port->snk_policy.src.pdo_count == 0) {
		return false;
	}

	if (port->snk_policy.pdo & PDO_FIXED_GET_UNCONSTRAINED_PWR ||
	    !(port->snk_policy.pdo & PDO_FIXED_DUAL_ROLE)) {
		return false;
	}

	if (charge_manager_get_active_charge_port() == port_num) {
		return false;
	}

	return true;
}

static void handle_dp_status(struct pdc_port_t *port)
{
	/* Check for an HPD wake on DP Status. The conditions are...
	 *  a) Device is suspended.
	 *  b) Port entered suspend in DP Alt Mode with HPD_LVL low.
	 *  c) Updated DP Status has HPD_LVL high.
	 */

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    port->hpd_wake_watch && PD_VDO_DPSTS_HPD_LVL(port->vdo_dp_status)) {
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
	}
}

static void run_snk_policies(struct pdc_port_t *port)
{
	const struct pdc_config_t *config = port->dev->config;
	int port_num = config->connector_num;

	if (atomic_test_and_clear_bit(port->snk_policy.flags,
				      SNK_POLICY_SET_ACTIVE_CHARGE_PORT)) {
		port->snk_attached_local_state = SNK_ATTACHED_SET_SINK_PATH;
		return;
	} else if (atomic_test_and_clear_bit(port->snk_policy.flags,
					     SNK_POLICY_SWAP_TO_SRC)) {
		port->pdr.swap_to_src = 1;
		port->pdr.swap_to_snk = 0;
		/* allow_pr_swap will be set by the src state entry code */
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	} else if (atomic_test_and_clear_bit(port->snk_policy.flags,
					     SNK_POLICY_NEW_POWER_REQUEST)) {
		port->snk_attached_local_state = SNK_ATTACHED_GET_PDOS;
		return;
	} else if (atomic_test_and_clear_bit(port->snk_policy.flags,
					     SNK_POLICY_EVAL_SWAP_TO_SRC)) {
		if (should_swap_to_source(port)) {
			atomic_set_bit(
				pdc_data[port_num]->port.snk_policy.flags,
				SNK_POLICY_SWAP_TO_SRC);
		}
		return;
	} else if (atomic_test_and_clear_bit(port->snk_policy.flags,
					     SNK_POLICY_UPDATE_ALLOW_PR_SWAP)) {
		port->pdr.accept_pr_swap =
			port->snk_policy.accept_power_role_swap;
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	} else if (atomic_test_and_clear_bit(port->snk_policy.flags,
					     SNK_POLICY_UPDATE_SRC_CAPS)) {
		/* Update the LPM with the correct SRC PDO in case there
		 * is a power role swap.
		 */
		queue_internal_cmd(port, CMD_PDC_SET_PDOS);
		return;
	}

	send_pending_public_commands(port);
}

static void run_typec_snk_policies(struct pdc_port_t *port)
{
	/* Note - hard resets specifically not checked for here.
	 * We don't expect hard resets while connected to a non-PD
	 * partner.
	 */
	if (atomic_test_and_clear_bit(port->snk_policy.flags,
				      SNK_POLICY_UPDATE_SRC_CAPS)) {
		/* Ensure the next time a PD capable SNK connects, we offer
		 * a safe PDO.
		 */
		queue_internal_cmd(port, CMD_PDC_SET_PDOS);
	} else {
		send_pending_public_commands(port);
	}
}

static void run_src_policies(struct pdc_port_t *port)
{
	const struct pdc_config_t *config = port->dev->config;
	int port_num = config->connector_num;

	if (atomic_test_and_clear_bit(port->src_policy.flags,
				      SRC_POLICY_SWAP_TO_SNK)) {
		port->pdr.swap_to_src = 0;
		port->pdr.swap_to_snk = 1;
		/* allow_pr_swap will be set by the snk state entry code */
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_FORCE_SNK)) {
		queue_internal_cmd(port, CMD_PDC_SET_CCOM);
		return;
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_EVAL_SNK_FIXED_PDO)) {
		/* Adjust source current limits if necessary */
		pdc_dpm_eval_sink_fixed_pdo(port_num,
					    port->src_policy.snk.pdos[0]);
		return;
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_UPDATE_SRC_CAPS)) {
		/* Update the PDC SRC_CAP message */
		queue_internal_cmd(port, CMD_PDC_SET_PDOS);
		/*
		 * After sending new SRC_CAP message, get the RDO from the port
		 * partner to see if the current limit can be adjusted.
		 */
		atomic_set_bit(port->src_policy.flags, SRC_POLICY_GET_RDO);
		return;
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_GET_RDO)) {
		/* Get the RDO from the port partner */
		queue_internal_cmd(port, CMD_PDC_GET_RDO);
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_UPDATE_ALLOW_PR_SWAP)) {
		port->pdr.accept_pr_swap =
			port->src_policy.accept_power_role_swap;
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		return;
	}

	send_pending_public_commands(port);
}

static void run_typec_src_policies(struct pdc_port_t *port)
{
	/* Check if Rp value needs to be adjusted */
	if (atomic_test_and_clear_bit(port->src_policy.flags,
				      SRC_POLICY_SET_RP)) {
		queue_internal_cmd(port, CMD_PDC_SET_POWER_LEVEL);
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_FORCE_SNK)) {
		queue_internal_cmd(port, CMD_PDC_SET_CCOM);
	} else if (atomic_test_and_clear_bit(port->src_policy.flags,
					     SRC_POLICY_UPDATE_SRC_CAPS)) {
		/* Ensure the next time a PD capable SNK connects, we offer
		 * a safe PDO.
		 */
		queue_internal_cmd(port, CMD_PDC_SET_PDOS);
	} else {
		send_pending_public_commands(port);
	}
}

/**
 * @brief Entering unattached state
 */
static void pdc_unattached_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *config = port->dev->config;
	int port_number = config->connector_num;

	print_current_pdc_state(port);

	set_attached_pdc_state(port, UNATTACHED_STATE);
	port->send_cmd.intern.pending = false;

	/* Clear any previously set cable property information */
	port->cable_prop.raw_value[0] = 0;
	port->cable_prop.raw_value[1] = 0;

	/* Ensure VDOs aren't valid from previous connection */
	discovery_info_init(port);

	/* Clear VBUS cache timeout. */
	port->vbus_expired = sys_timepoint_calc(K_NO_WAIT);

	if (get_pdc_state(port) != port->send_cmd_return_state) {
		invalidate_charger_settings(port);
		port->unattached_local_state = UNATTACHED_SET_SINK_PATH_OFF;
		/* Update source current limit policy */
		pdc_dpm_remove_sink(port_number);
		pdc_dpm_remove_source(port_number);

		/* Clear all events except for disconnect. */
		pdc_power_mgmt_clear_event(port_number,
					   BIT_MASK(PD_STATUS_EVENT_COUNT));
		pdc_power_mgmt_notify_event(port_number,
					    PD_STATUS_EVENT_DISCONNECTED);
	}
}

/**
 * @brief Run unattached state
 */
static void pdc_unattached_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* The CCI_EVENT is set to re-query connector status, so check the
	 * connector status and take the appropriate action.
	 */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ACK)) {
		queue_internal_cmd(port, CMD_PDC_ACK_CC_CI);
		return;
	}

	switch (port->unattached_local_state) {
	case UNATTACHED_SET_SINK_PATH_OFF:
		port->sink_path_en = false;
		port->unattached_local_state = UNATTACHED_RUN;
		queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
		return;
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
	set_attached_pdc_state(port, SRC_ATTACHED_STATE);

	port->send_cmd.intern.pending = false;

	if (get_pdc_state(port) != port->send_cmd_return_state) {
		invalidate_charger_settings(port);
		port->src_attached_local_state = SRC_ATTACHED_SET_SINK_PATH_OFF;
	}

	/* Clear a piece of sink policy as it is no longer relevant in the
	 * sourcing state.
	 */
	atomic_clear_bit(port->snk_policy.flags, SNK_POLICY_EVAL_SWAP_TO_SRC);
}

/**
 * @brief Run source attached state
 */
static void pdc_src_attached_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	/* The CCI_EVENT is set to re-query connector status, so check the
	 * connector status and take the appropriate action.
	 */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ACK)) {
		queue_internal_cmd(port, CMD_PDC_ACK_CC_CI);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_CAM_CHANGE)) {
		queue_internal_cmd(port, CMD_PDC_GET_PD_VDO_DP_CFG_SELF);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ATTENTION)) {
		queue_internal_cmd(port, CMD_PDC_GET_PD_VDO_DP_STATUS);
		return;
	}

	/* TODO: b/319643480 - Brox: implement SRC policies */

	switch (port->src_attached_local_state) {
	case SRC_ATTACHED_SET_SINK_PATH_OFF:
		port->sink_path_en = false;
		port->src_attached_local_state =
			SRC_ATTACHED_GET_CONNECTOR_CAPABILITY;
		queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
		return;
	case SRC_ATTACHED_GET_CONNECTOR_CAPABILITY:
		port->src_attached_local_state =
			SRC_ATTACHED_GET_CABLE_PROPERTY;
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_CAPABILITY);
		return;
	case SRC_ATTACHED_GET_CABLE_PROPERTY:
		port->src_attached_local_state =
			SRC_ATTACHED_SET_DR_SWAP_POLICY;
		queue_internal_cmd(port, CMD_PDC_GET_CABLE_PROPERTY);
		return;
	case SRC_ATTACHED_SET_DR_SWAP_POLICY:
		port->src_attached_local_state =
			SRC_ATTACHED_SET_PR_SWAP_POLICY;
		/* TODO read from DT */
		port->uor.swap_to_dfp = 1;
		port->uor.swap_to_ufp = 0;
		port->uor.accept_dr_swap = 1;
		queue_internal_cmd(port, CMD_PDC_SET_UOR);
		return;
	case SRC_ATTACHED_SET_PR_SWAP_POLICY:
		port->src_attached_local_state = SRC_ATTACHED_READ_POWER_LEVEL;
		/* TODO: read from DT */
		port->pdr = (union pdr_t){
			.accept_pr_swap =
				port->src_policy.accept_power_role_swap,
			.swap_to_src = 1,
			.swap_to_snk = 0,
		};
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		atomic_clear_bit(port->src_policy.flags,
				 SRC_POLICY_UPDATE_ALLOW_PR_SWAP);
		return;
	case SRC_ATTACHED_READ_POWER_LEVEL:
		port->src_attached_local_state = SRC_ATTACHED_GET_VDO;
		queue_internal_cmd(port, CMD_PDC_READ_POWER_LEVEL);
		return;
	case SRC_ATTACHED_GET_VDO:
		port->src_attached_local_state = SRC_ATTACHED_GET_PDOS;
		queue_internal_cmd(port, CMD_PDC_GET_VDO);
		return;
	case SRC_ATTACHED_GET_PDOS:
		/* Request up to 4 pdos to honor USCI 6.5.15 Get PDOs - Number
		 * of PDOs to return starting from the PDO Offset. The number of
		 * PDOs to return is the value in this field plus 1.
		 */
		if (!port->get_pdo.updating) {
			port->get_pdo.num_pdos = PDO_NUM;
			port->get_pdo.pdo_offset = PDO_OFFSET_0;
			port->get_pdo.updating = true;
		}
		if (port->get_pdo.num_pdos > 4) {
			port->src_attached_local_state = SRC_ATTACHED_GET_PDOS;
		} else {
			port->src_attached_local_state = SRC_ATTACHED_RUN;
			port->get_pdo.updating = false;
		}
		port->get_pdo.pdo_type = SINK_PDO;
		port->get_pdo.pdo_source = PARTNER_PDO;
		queue_internal_cmd(port, CMD_PDC_GET_PDOS);
		/* Evaluate SNK CAP after it's been retrieved from the PDC */
		atomic_set_bit(port->src_policy.flags,
			       SRC_POLICY_EVAL_SNK_FIXED_PDO);
		return;
	case SRC_ATTACHED_RUN:
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
	set_attached_pdc_state(port, SNK_ATTACHED_STATE);

	port->send_cmd.intern.pending = false;
	if (get_pdc_state(port) != port->send_cmd_return_state) {
		const struct pdc_config_t *config = port->dev->config;
		int port_number = config->connector_num;

		port->snk_attached_local_state =
			SNK_ATTACHED_GET_CONNECTOR_CAPABILITY;

		/* If we were just a SRC, tell the DPM that the
		 * attached sink has been disconnected.
		 */
		pdc_dpm_remove_sink(port_number);
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
	uint32_t tmp_curr_ma, tmp_volt_mv, tmp_pwr_mw;
	uint32_t pdo_pwr_mw, pdo_volt_mv;
	uint32_t flags;

	/* The CCI_EVENT is set to re-query connector status, so check the
	 * connector status and take the appropriate action.
	 */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ACK)) {
		queue_internal_cmd(port, CMD_PDC_ACK_CC_CI);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_CAM_CHANGE)) {
		queue_internal_cmd(port, CMD_PDC_GET_PD_VDO_DP_CFG_SELF);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ATTENTION)) {
		queue_internal_cmd(port, CMD_PDC_GET_PD_VDO_DP_STATUS);
		return;
	}

	switch (port->snk_attached_local_state) {
	case SNK_ATTACHED_GET_CONNECTOR_CAPABILITY:
		port->snk_attached_local_state =
			SNK_ATTACHED_GET_CABLE_PROPERTY;
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_CAPABILITY);
		return;
	case SNK_ATTACHED_GET_CABLE_PROPERTY:
		port->snk_attached_local_state =
			SNK_ATTACHED_SET_DR_SWAP_POLICY;
		queue_internal_cmd(port, CMD_PDC_GET_CABLE_PROPERTY);
		return;
	case SNK_ATTACHED_SET_DR_SWAP_POLICY:
		port->snk_attached_local_state =
			SNK_ATTACHED_SET_PR_SWAP_POLICY;
		/* TODO read from DT */
		port->uor.swap_to_dfp = 1;
		port->uor.swap_to_ufp = 0;
		port->uor.accept_dr_swap = 1;
		queue_internal_cmd(port, CMD_PDC_SET_UOR);
		return;
	case SNK_ATTACHED_SET_PR_SWAP_POLICY:
		port->snk_attached_local_state = SNK_ATTACHED_GET_VDO;
		/* TODO: read from DT */
		port->pdr = (union pdr_t){
			.accept_pr_swap =
				port->snk_policy.accept_power_role_swap,
			.swap_to_src = 0,
			.swap_to_snk = 1,
		};
		queue_internal_cmd(port, CMD_PDC_SET_PDR);
		atomic_clear_bit(port->snk_policy.flags,
				 SNK_POLICY_UPDATE_ALLOW_PR_SWAP);
		return;
	case SNK_ATTACHED_GET_VDO:
		port->snk_attached_local_state = SNK_ATTACHED_GET_PDOS;
		queue_internal_cmd(port, CMD_PDC_GET_VDO);
		return;
	case SNK_ATTACHED_GET_PDOS:
		/* Request up to 4 pdos to honor USCI 6.5.15 Get PDOs - Number
		 * of PDOs to return starting from the PDO Offset. The number of
		 * PDOs to return is the value in this field plus 1.
		 */
		if (!port->get_pdo.updating) {
			port->get_pdo.num_pdos = PDO_NUM;
			port->get_pdo.pdo_offset = PDO_OFFSET_0;
			port->get_pdo.updating = true;
		}
		if (port->get_pdo.num_pdos > 4) {
			port->snk_attached_local_state = SNK_ATTACHED_GET_PDOS;
		} else {
			port->snk_attached_local_state =
				SNK_ATTACHED_EVALUATE_PDOS;
			port->get_pdo.updating = false;
		}
		port->get_pdo.pdo_type = SOURCE_PDO;
		port->get_pdo.pdo_source = PARTNER_PDO;
		queue_internal_cmd(port, CMD_PDC_GET_PDOS);
		return;
	case SNK_ATTACHED_EVALUATE_PDOS:
		port->snk_attached_local_state = SNK_ATTACHED_START_CHARGING;
		pdo_pwr_mw = 0;
		pdo_volt_mv = 0;
		flags = 0;

		for (int i = 0; i < PDO_NUM; i++) {
			if ((port->snk_policy.src.pdos[i] & PDO_TYPE_MASK) !=
			    PDO_TYPE_FIXED) {
				continue;
			}

			tmp_volt_mv = PDO_FIXED_GET_VOLT(
				port->snk_policy.src.pdos[i]);
			tmp_curr_ma = PDO_FIXED_GET_CURR(
				port->snk_policy.src.pdos[i]);
			tmp_pwr_mw = (tmp_volt_mv * tmp_curr_ma) / 1000;

			LOG_INF("PDO%d: %08x, %d %d %d", i,
				port->snk_policy.src.pdos[i], tmp_volt_mv,
				tmp_curr_ma, tmp_pwr_mw);

			if ((tmp_pwr_mw >= pdo_pwr_mw) &&
			    (tmp_pwr_mw <= pdc_max_operating_power) &&
			    (tmp_volt_mv <= pdc_max_request_mv))
				if ((tmp_pwr_mw > pdo_pwr_mw) ||
				    (tmp_volt_mv > pdo_volt_mv)) {
					pdo_pwr_mw = tmp_pwr_mw;
					pdo_volt_mv = tmp_volt_mv;
					port->snk_policy.pdo_index = i;
					port->snk_policy.pdo =
						port->snk_policy.src.pdos[i];
				}
		}

		/* Extract Current, Voltage, and calculate Power */
		max_ma = PDO_FIXED_GET_CURR(port->snk_policy.pdo);
		max_mv = PDO_FIXED_GET_VOLT(port->snk_policy.pdo);
		max_mw = max_ma * max_mv / 1000;

		/* Mismatch bit set if less power offered than the operating
		 * power */
		if (max_mw < pdc_max_operating_power) {
			flags |= RDO_CAP_MISMATCH;
		}

		/* Prepare PDO index for creation of RDO */
		port->snk_policy.pdo_index += 1;

		/* Set RDO to send */
		if ((port->snk_policy.pdo & PDO_TYPE_MASK) ==
		    PDO_TYPE_BATTERY) {
			port->snk_policy.rdo_to_send =
				RDO_BATT(port->snk_policy.pdo_index, max_mw,
					 max_mw, flags);
		} else {
			port->snk_policy.rdo_to_send =
				RDO_FIXED(port->snk_policy.pdo_index, max_ma,
					  max_ma, flags);
		}

		LOG_INF("Send RDO: %d", RDO_POS(port->snk_policy.rdo_to_send));
		queue_internal_cmd(port, CMD_PDC_SET_RDO);
		return;
	case SNK_ATTACHED_START_CHARGING:
		max_ma = PDO_FIXED_GET_CURR(port->snk_policy.pdo);
		max_mv = PDO_FIXED_GET_VOLT(port->snk_policy.pdo);
		max_mw = max_ma * max_mv / 1000;

		LOG_INF("Available charging on C%d", config->connector_num);
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

		port->snk_attached_local_state = SNK_ATTACHED_GET_RDO;
		break;
	case SNK_ATTACHED_GET_RDO:
		port->snk_attached_local_state = SNK_ATTACHED_SET_SINK_PATH;
		queue_internal_cmd(port, CMD_PDC_GET_RDO);
		return;
	case SNK_ATTACHED_SET_SINK_PATH:
		port->snk_attached_local_state = SNK_ATTACHED_RUN;

		/* Test if battery can be charged from this port */
		port->sink_path_en = port->active_charge;
		queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
		return;
	case SNK_ATTACHED_RUN:
		/* Hard Reset could disable Sink FET. Re-enable it */
		if (atomic_get(&port->hard_reset_sent)) {
			atomic_clear(&port->hard_reset_sent);
			port->snk_attached_local_state =
				SNK_ATTACHED_SET_SINK_PATH;
		} else {
			run_snk_policies(port);
		}
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
}

static int send_pdc_cmd(struct pdc_port_t *port)
{
	int rv;
	const struct pdc_config_t *const config = port->dev->config;
	uint32_t *rdo;

	LOG_DBG("C%d: Send %s (%d) %s", config->connector_num,
		pdc_cmd_names[port->cmd->cmd], port->cmd->cmd,
		(port->cmd == &port->send_cmd.intern) ? "internal" : "public");

	/* Send PDC command via driver API */
	switch (port->cmd->cmd) {
	case CMD_PDC_RESET:
		rv = pdc_reset(port->pdc);
		break;
	case CMD_PDC_GET_INFO:
		rv = pdc_get_info(port->pdc, &port->info, true);
		break;
	case CMD_PDC_SET_POWER_LEVEL:
		rv = pdc_set_power_level(port->pdc, port->una_policy.tcc);
		break;
	case CMD_PDC_SET_CCOM:
		rv = pdc_set_ccom(port->pdc, port->una_policy.cc_mode);
		break;
	case CMD_PDC_SET_DRP:
		rv = pdc_set_drp_mode(port->pdc, port->drp);
		break;
	case CMD_PDC_GET_PDOS:
		rv = pdc_get_pdos(
			port->pdc, port->get_pdo.pdo_type,
			port->get_pdo.pdo_offset,
			port->get_pdo.num_pdos > 4 ? 4 : port->get_pdo.num_pdos,
			port->get_pdo.pdo_source,
			get_pdc_pdos_ptr(port, &port->get_pdo)->pdos +
				port->get_pdo.pdo_offset);
		if (!rv && port->get_pdo.num_pdos > 4) {
			port->get_pdo.num_pdos -= 4;
			port->get_pdo.pdo_offset = PDO_OFFSET_4;
		}
		break;
	case CMD_PDC_GET_RDO:
		/* RDO from LPM or port partner depending on power role */
		if (port->attached_state == SRC_ATTACHED_STATE) {
			rdo = &port->src_policy.rdo;
		} else {
			rdo = &port->snk_policy.rdo;
		}
		rv = pdc_get_rdo(port->pdc, rdo);
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
	case CMD_PDC_GET_CABLE_PROPERTY:
		rv = pdc_get_cable_property(port->pdc, &port->cable_prop);
		break;
	case CMD_PDC_GET_VDO:
		rv = pdc_get_vdo(port->pdc, port->vdo_req, port->vdo_type,
				 port->vdo);
		break;
	case CMD_PDC_GET_PD_VDO_DP_CFG_SELF: {
		union get_vdo_t vdo_req;
		uint8_t vdo_type;

		vdo_req.raw_value = 0;
		vdo_req.num_vdos = 1;
		vdo_req.vdo_origin = VDO_ORIGIN_PORT;

		vdo_type = VDO_PD_DP_CFG;

		rv = pdc_get_vdo(port->pdc, vdo_req, &vdo_type,
				 &port->vdo_dp_cfg);
		break;
	}
	case CMD_PDC_GET_PD_VDO_DP_STATUS: {
		union get_vdo_t vdo_req;
		uint8_t vdo_type;

		vdo_req.raw_value = 0;
		vdo_req.num_vdos = 1;
		vdo_req.vdo_origin = VDO_ORIGIN_SOP;

		vdo_type = VDO_PD_DP_STATUS;

		rv = pdc_get_vdo(port->pdc, vdo_req, &vdo_type,
				 &port->vdo_dp_status);
		break;
	}
	case CMD_PDC_CONNECTOR_RESET:
		rv = pdc_connector_reset(port->pdc, port->connector_reset);
		break;
	case CMD_PDC_GET_IDENTITY_DISCOVERY:
		rv = pdc_get_identity_discovery(port->pdc,
						&port->discovery_state);
		break;
	case CMD_PDC_IS_VCONN_SOURCING:
		if (port->public_api_buff == NULL) {
			return -EINVAL;
		}
		rv = pdc_is_vconn_sourcing(port->pdc,
					   (bool *)port->public_api_buff);
		break;
	case CMD_PDC_SET_PDOS:
		rv = pdc_set_pdos(port->pdc, port->set_pdos.type,
				  port->set_pdos.pdos, port->set_pdos.count);
		break;
	case CMD_PDC_GET_PCH_DATA_STATUS:
		rv = pdc_get_pch_data_status(port->pdc, config->connector_num,
					     port->pch_data_status);
		break;
	case CMD_PDC_ACK_CC_CI:
		rv = pdc_ack_cc_ci(port->pdc, port->ci, port->cc,
				   port->vendor_defined_ci);
		break;
	case CMD_PDC_GET_LPM_PPM_INFO:
		rv = pdc_get_lpm_ppm_info(port->pdc, port->lpm_ppm_info);
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
	 * previous command, which previously didn't complete or fail within
	 * WAIT_MAX. This flag is only meaningful for the command that was just
	 * sent to the PDC.
	 */
	atomic_clear_bit(port->cci_flags, CCI_CMD_COMPLETED);
	atomic_clear_bit(port->cci_flags, CCI_ERROR);

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
			port->cmd->error = true;
			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
	} else if (atomic_test_and_clear_bit(port->cci_flags,
					     CCI_CMD_COMPLETED)) {
		LOG_DBG("CCI_CMD_COMPLETED");
		if (port->cmd->cmd == CMD_PDC_GET_CONNECTOR_STATUS) {
			if (handle_connector_status(port)) {
				return;
			}
		} else {
			if (port->cmd->cmd == CMD_PDC_GET_PD_VDO_DP_STATUS) {
				handle_dp_status(port);
			}

			set_pdc_state(port, port->send_cmd_return_state);
			return;
		}
		/*
		 * Note: If the command was CONNECTOR_RESET, and the type of
		 * reset was a Hard Reset, then it would also make sense to
		 * notify the host of PD_STATUS_EVENT_HARD_RESET. However, this
		 * would be redundant with the notification that will be
		 * generated later, upon completion of GET_CONNECTOR_STATUS.
		 */
	} else {
		/* No response: Wait until timeout. */
		port->send_cmd.wait_counter++;
		if (port->send_cmd.wait_counter > WAIT_MAX) {
			port->cmd->error = true;
			if (port->cmd->cmd == CMD_PDC_GET_CONNECTOR_STATUS) {
				/*
				 * Can't get connector status. Enter unattached
				 * state with error flag set, so it can reset
				 * the PDC.
				 */
				port->cmd->cmd = CMD_PDC_RESET;
				set_pdc_state(port, PDC_UNATTACHED);
				return;
			} else {
				set_pdc_state(port,
					      port->send_cmd_return_state);
				return;
			}
		}
	}
}

static void pdc_send_cmd_wait_exit(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *const config = port->dev->config;
	struct pdc_pdos_t *pdc_pdos;

	if (port->cmd == &port->send_cmd.public) {
		k_event_post(&port->sm_event, PDC_PUBLIC_CMD_COMPLETE_EVENT);
	}

	/* Completed with error. Clear complete bit */
	atomic_clear_bit(port->cci_flags, CCI_CMD_COMPLETED);
	port->cmd->pending = false;

	switch (port->cmd->cmd) {
	case CMD_PDC_GET_PDOS:
		/* Get pointer to struct for pdos array and count */
		pdc_pdos = get_pdc_pdos_ptr(port, &port->get_pdo);
		pdc_pdos->pdo_count = 0;

		/* Filter out Augmented Power Data Objects (APDO). APDOs come
		 * after the regular PDOS, so it's safe to exclude them from the
		 * pdo_count. */
		/* TODO This is temporary until APDOs can be handled  */
		for (int i = 0; i < PDO_NUM; i++) {
			if (pdc_pdos->pdos[i] & PDO_TYPE_AUGMENTED) {
				pdc_pdos->pdos[i] = 0;
			} else {
				pdc_pdos->pdo_count++;
			}
		}
		break;
	case CMD_PDC_GET_RDO:
		if (port->attached_state == SRC_ATTACHED_STATE) {
			/* Inform DPM port partner's current request */
			pdc_dpm_evaluate_request_rdo(config->connector_num,
						     port->src_policy.rdo);
		}
		break;
	case CMD_PDC_SET_PDR:
	case CMD_PDC_SET_UOR:
	case CMD_PDC_SET_PDOS:
	case CMD_PDC_SET_SINK_PATH:
		trigger_ppm_status_change(port);
		break;
	default:
		break;
	}
}

static void pdc_src_typec_only_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);
	set_attached_pdc_state(port, SRC_ATTACHED_TYPEC_ONLY_STATE);

	if (get_pdc_state(port) != port->send_cmd_return_state) {
		port->src_typec_attached_local_state =
			SRC_TYPEC_ATTACHED_SET_SINK_PATH_OFF;

		/* Start one shot typec only timer. This timer is used to
		 * differentiate between a port partner that supports USB PD or
		 * is typec_only. Note that the timer is not explicitly
		 * stopped. Since there is no callback associated, letting it
		 * expire in the src.attached state will have no effect and the
		 * k_timer_start call always resets the timer status.
		 */
		k_timer_start(&port->typec_only_timer,
			      K_USEC(PD_T_SINK_WAIT_CAP), K_NO_WAIT);
	}
}

static void pdc_src_typec_only_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *config = port->dev->config;
	int port_number = config->connector_num;

	set_attached_pdc_state(port, SRC_ATTACHED_TYPEC_ONLY_STATE);

	/* The CCI_EVENT is set on a connector disconnect, so check the
	 * connector status and take the appropriate action. */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ACK)) {
		queue_internal_cmd(port, CMD_PDC_ACK_CC_CI);
		return;
	}

	switch (port->src_typec_attached_local_state) {
	case SRC_TYPEC_ATTACHED_SET_SINK_PATH_OFF:
		port->src_typec_attached_local_state =
			SRC_TYPEC_ATTACHED_DEBOUNCE;

		port->sink_path_en = false;
		queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
		return;
	case SRC_TYPEC_ATTACHED_DEBOUNCE:
		if (k_timer_status_get(&port->typec_only_timer) > 0) {
			port->src_typec_attached_local_state =
				SRC_TYPEC_ATTACHED_ADD_SINK;
		}
		return;
	case SRC_TYPEC_ATTACHED_ADD_SINK:
		port->src_typec_attached_local_state = SRC_TYPEC_ATTACHED_RUN;
		/* Notify DPM that a type-c only port partner is attached */
		pdc_dpm_add_non_pd_sink(port_number);
		return;
	case SRC_TYPEC_ATTACHED_RUN:
		run_typec_src_policies(port);
		break;
	}
}

static void pdc_snk_typec_only_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	port->send_cmd.intern.pending = false;
	if (get_pdc_state(port) != port->send_cmd_return_state) {
		port->snk_typec_attached_local_state =
			SNK_TYPEC_ATTACHED_SET_CHARGE_CURRENT;

		/* Start one shot typec only timer. This timer is used to
		 * differentiate between a port partner that supports USB PD or
		 * is typec_only. Note that the timer is not explicitly
		 * stopped. Since there is no callback associated, letting it
		 * expire in the snk.attached state will have no effect and the
		 * k_timer_start call always resets the timer status.
		 */
		k_timer_start(&port->typec_only_timer,
			      K_USEC(PD_T_SINK_WAIT_CAP), K_NO_WAIT);
	}

	print_current_pdc_state(port);
	set_attached_pdc_state(port, SNK_ATTACHED_TYPEC_ONLY_STATE);
}

static void pdc_snk_typec_only_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *const config = port->dev->config;

	set_attached_pdc_state(port, SNK_ATTACHED_TYPEC_ONLY_STATE);

	/* The CCI_EVENT is set to re-query connector status, so check the
	 * connector status and take the appropriate action.
	 */
	if (atomic_test_and_clear_bit(port->cci_flags, CCI_EVENT)) {
		queue_internal_cmd(port, CMD_PDC_GET_CONNECTOR_STATUS);
		return;
	}

	if (atomic_test_and_clear_bit(port->cci_flags, CCI_ACK)) {
		queue_internal_cmd(port, CMD_PDC_ACK_CC_CI);
		return;
	}

	switch (port->snk_typec_attached_local_state) {
	case SNK_TYPEC_ATTACHED_SET_CHARGE_CURRENT:
		port->snk_typec_attached_local_state =
			SNK_TYPEC_ATTACHED_SET_SINK_PATH_ON;

		typec_set_input_current_limit(config->connector_num,
					      port->typec_current_ma, 5000);

		charge_manager_update_dualrole(config->connector_num,
					       CAP_DEDICATED);
		break;
	case SNK_TYPEC_ATTACHED_SET_SINK_PATH_ON:
		port->snk_typec_attached_local_state =
			SNK_TYPEC_ATTACHED_DEBOUNCE;
		port->sink_path_en = true;
		queue_internal_cmd(port, CMD_PDC_SET_SINK_PATH);
		return;
	case SNK_TYPEC_ATTACHED_DEBOUNCE:
		if (k_timer_status_get(&port->typec_only_timer) > 0) {
			port->snk_typec_attached_local_state =
				SNK_TYPEC_ATTACHED_RUN;
		}
		return;
	case SNK_TYPEC_ATTACHED_RUN:
		run_typec_snk_policies(port);
		break;
	}
}

static void pdc_init_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);

	/* Initialize Send Command data */
	send_cmd_init(port);
	/* Set up GET_VDO command data */
	discovery_info_init(port);
}

/**
 * @brief Chipset Resume (S3->S0) Policy 1: Set a flag to perform a one-time
 *        test if we should swap to a source role. (applicable only if we are
 *        currently a sink)
 */
static void enforce_pd_chipset_resume_policy_1(int port)
{
	LOG_DBG("Chipset Resume Policy 1");

	/* If we're in a sink role, run a check to determine if we'd prefer a
	 * source role.
	 */
	atomic_set_bit(pdc_data[port]->port.snk_policy.flags,
		       SNK_POLICY_EVAL_SWAP_TO_SRC);
}

/*
 * PD policy handlers
 *
 * These functions are triggered by AP power state changes via hooks and also
 * through the PDC power management state machine's init state in cases when a
 * late system jump happened.
 *
 * These functions should set flags to trigger actions from within the state
 * machine, rather than performing operations directly.
 */

/**
 * @brief Chipset Resume (S3->S0) Policy 2:
 *	a) DRP Toggle ON
 */
static void enforce_pd_chipset_resume_policy_2(int port)
{
	LOG_DBG("C%d: Chipset Resume Policy 2", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_ON);
}

/**
 * @brief Chipset Suspend (S0->S3) Policy 1:
 *	a) DRP TOGGLE OFF
 */
static void enforce_pd_chipset_suspend_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Suspend Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_OFF);
}

/**
 * @brief Chipset Startup (S5->S3) Policy 1:
 *	a) DRP Toggle OFF
 */
static void enforce_pd_chipset_startup_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Startup Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_TOGGLE_OFF);
}

/**
 * Chipset Shutdown (S3->S5) Policy 1:
 *	a) DRP Force SINK
 */
static void enforce_pd_chipset_shutdown_policy_1(int port)
{
	LOG_DBG("C%d: Chipset Shutdown Policy 1", port);

	pdc_power_mgmt_set_dual_role(port, PD_DRP_FORCE_SINK);
}

static void set_hpd_wake_watch(int port);

static void clear_hpd_wake_watch(int port);

/**
 * @brief Apply correct policy based on system power state
 *
 * This is normally triggered by hooks on AP power state changes
 * (HOOK_CHIPSET_RESUME, etc) elsewhere in this file. The hooks enforce
 * hysteresis on the power state to avoid rapid policy flapping.
 *
 * In the case of a late sysjump, this function is also called during
 * init to force the correct policy, since the normal start-up power
 * state transition hooks will not be occur.
 *
 * Note: this should run once, and not per-port.
 */
static void pdc_apply_power_state_policy(struct k_work *work)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		LOG_INF("PD: AP is ON: apply 'startup' followed by 'resume'");
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			enforce_pd_chipset_startup_policy_1(i);
			/*
			 * Setting the dual role state clears the policy flag
			 * SNK_POLICY_SWAP_TO_SRC which may get set in
			 * enforce_pd_chipset_resume_policy_1() so this policy
			 * function needs to be called after resume_policy_2()
			 * which sets DRP mode on.
			 */
			enforce_pd_chipset_resume_policy_2(i);
			enforce_pd_chipset_resume_policy_1(i);
			clear_hpd_wake_watch(i);
		}
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		LOG_INF("PD: AP is SUSPENDED: apply 'suspend' policy");
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			enforce_pd_chipset_suspend_policy_1(i);
			set_hpd_wake_watch(i);
		}
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		LOG_INF("PD: AP is OFF: apply 'shutdown' policy");
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			enforce_pd_chipset_shutdown_policy_1(i);
		}
	}
}

static K_WORK_DELAYABLE_DEFINE(pdc_apply_power_state_policy_work,
			       pdc_apply_power_state_policy);

/**
 * @brief Returns true if all PDC port drivers have finished initializing
 *
 * @return bool True if all ports are ready, false if still pending.
 */
static bool pdc_all_ports_ready(void)
{
	for (uint8_t i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		if (!pdc_is_init_done(pdc_data[i]->port.pdc)) {
			return false;
		}
	}
	return true;
}

static void pdc_init_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;
	const struct pdc_config_t *const config = port->dev->config;

	/* Wait until PDC driver is initialized */
	if (pdc_is_init_done(port->pdc)) {
		LOG_INF("C%d: PDC Subsystem Started", config->connector_num);
		/* Apply policy in case of a late sysjump since we won't receive
		 * the usual hook calls upon AP power state changes. Only called
		 * once, after all port drivers are ready.
		 */
		if (system_jumped_late() && pdc_all_ports_ready()) {
			LOG_INF("PD: Handling late sysjump");
			pdc_apply_power_state_policy(
				&pdc_apply_power_state_policy_work.work);
		}

		/* Send the connector status command to determine which state to
		 * enter
		 */
		port->send_cmd.intern.cmd = CMD_PDC_GET_CONNECTOR_STATUS;
		port->send_cmd.intern.pending = true;
		port->public_api_buff = NULL;
		set_pdc_state(port, PDC_SEND_CMD_START);
		return;
	}
}

static void pdc_suspended_entry(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	print_current_pdc_state(port);
}

static void pdc_suspended_run(void *obj)
{
	struct pdc_port_t *port = (struct pdc_port_t *)obj;

	if (atomic_get(&port->suspend)) {
		/* Still suspended. Do nothing. */
		return;
	}

	/* No longer suspended. Do a full reset. */
	init_port_variables(port);
	set_pdc_state(port, PDC_INIT);
}

/**
 * @brief Populate state table
 */
static const struct smf_state pdc_states[] = {
	/* Normal States */
	[PDC_INIT] = SMF_CREATE_STATE(pdc_init_entry, pdc_init_run, NULL, NULL,
				      NULL),
	[PDC_UNATTACHED] = SMF_CREATE_STATE(
		pdc_unattached_entry, pdc_unattached_run, NULL, NULL, NULL),
	[PDC_SNK_ATTACHED] = SMF_CREATE_STATE(
		pdc_snk_attached_entry, pdc_snk_attached_run, NULL, NULL, NULL),
	[PDC_SRC_ATTACHED] = SMF_CREATE_STATE(
		pdc_src_attached_entry, pdc_src_attached_run, NULL, NULL, NULL),
	[PDC_SEND_CMD_START] = SMF_CREATE_STATE(pdc_send_cmd_start_entry,
						pdc_send_cmd_start_run, NULL,
						NULL, NULL),
	[PDC_SEND_CMD_WAIT] =
		SMF_CREATE_STATE(pdc_send_cmd_wait_entry, pdc_send_cmd_wait_run,
				 pdc_send_cmd_wait_exit, NULL, NULL),
	[PDC_SRC_TYPEC_ONLY] = SMF_CREATE_STATE(pdc_src_typec_only_entry,
						pdc_src_typec_only_run, NULL,
						NULL, NULL),
	[PDC_SNK_TYPEC_ONLY] = SMF_CREATE_STATE(pdc_snk_typec_only_entry,
						pdc_snk_typec_only_run, NULL,
						NULL, NULL),
	[PDC_SUSPENDED] = SMF_CREATE_STATE(pdc_suspended_entry,
					   pdc_suspended_run, NULL, NULL, NULL),
};

/**
 * @brief CCI event handler call back
 */
static void pdc_cc_handler_cb(const struct device *dev,
			      const struct pdc_callback *callback,
			      union cci_event_t cci_event)
{
	struct pdc_port_t *port =
		CONTAINER_OF(callback, struct pdc_port_t, cc_cb);
	bool post_event = false;

	/* Handle busy event from driver */
	if (cci_event.busy) {
		atomic_set_bit(port->cci_flags, CCI_BUSY);
		post_event = true;
	}

	/* Handle error event from driver */
	if (cci_event.error) {
		atomic_set_bit(port->cci_flags, CCI_ERROR);
		post_event = true;
	}

	/* Handle command completed event from driver */
	if (cci_event.command_completed) {
		atomic_set_bit(port->cci_flags, CCI_CMD_COMPLETED);
		post_event = true;
	}

	if (post_event)
		k_event_post(&port->sm_event, PDC_SM_EVENT);
}

static void pdc_ci_handler_cb(const struct device *dev,
			      const struct pdc_callback *callback,
			      union cci_event_t cci_event)
{
	struct pdc_port_t *port =
		CONTAINER_OF(callback, struct pdc_port_t, ci_cb);
	const struct pdc_config_t *const config = port->dev->config;
	bool post_event = false;

	/* Handle generic vendor defined event from driver */
	if (cci_event.vendor_defined_indicator) {
		atomic_set_bit(port->cci_flags, CCI_EVENT);
		post_event = true;
	}

	if (cci_event.connector_change == config->connector_num + 1) {
		atomic_set_bit(port->cci_flags, CCI_PPM_EVENT);
		post_event = true;
	}

	if (post_event)
		k_event_post(&port->sm_event, PDC_SM_EVENT);
}

static void init_port_variables(struct pdc_port_t *port)
{
	/* This also seeds the Charge Manager */
	invalidate_charger_settings(port);

	/* Init port variables */

	atomic_clear(port->pdc_cmd_flags);
	atomic_clear(port->cci_flags);
	port->port_event = ATOMIC_INIT(0);
	port->get_pdo.updating = false;

	/* Can charge from port by default */
	port->active_charge = true;

	port->last_state = PDC_INIT;
	port->next_state = PDC_INIT;
}

/**
 * @brief Initialize the PDC Subsystem
 */
static int pdc_subsys_init(const struct device *dev)
{
	struct pdc_data_t *data = dev->data;
	struct pdc_port_t *port = &data->port;
	const struct pdc_config_t *const config = dev->config;
	int rv;

	/* Make sure PD Controller is ready */
	if (!device_is_ready(port->pdc)) {
		LOG_ERR("PDC not ready");
		k_oops();
		/* Unreachable */
		return -ENODEV;
	}

	init_port_variables(port);

	/* Set cc call back */
	port->cc_cb.handler = pdc_cc_handler_cb;
	pdc_set_cc_callback(port->pdc, &port->cc_cb);

	/* Set ci call back */
	port->ci_cb.handler = pdc_ci_handler_cb;
	rv = pdc_add_ci_callback(port->pdc, &port->ci_cb);
	if (rv)
		LOG_ERR("Failed to add CI callback (%d)", rv);

	/* Initialize state machine run event */
	k_event_init(&port->sm_event);

	/* Initialize command mutex */
	k_mutex_init(&port->mtx);
	smf_set_initial(&port->ctx, &pdc_states[PDC_INIT]);

	/* Initialize typec only timer */
	k_timer_init(&port->typec_only_timer, NULL, NULL);

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
	case CMD_PDC_SET_POWER_LEVEL:
		__fallthrough;
	case CMD_PDC_GET_INFO:
		__fallthrough;
	case CMD_PDC_GET_PCH_DATA_STATUS:
		__fallthrough;
	case CMD_PDC_GET_LPM_PPM_INFO:
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
	int ret;
	struct cmd_t *public_cmd;
	k_timepoint_t cmd_timepoint;

	ret = queue_public_cmd(&pdc_data[port]->port, pdc_cmd);
	if (ret) {
		LOG_ERR("C%d: Could not queue %s: %d", port,
			pdc_cmd_names[pdc_cmd], ret);
		return ret;
	}

	public_cmd = &pdc_data[port]->port.send_cmd.public;

	/* TODO: Investigate using a semaphore here instead of while loop */
	/* Block calling thread until command is processed, errors or timeout
	 * occurs. */
	cmd_timepoint = sys_timepoint_calc(K_MSEC(PDC_CMD_TIMEOUT_MS));

	while (public_cmd->pending && !public_cmd->error) {
		/* block until command completes or max block count is reached
		 */

		/* Wait for timeout or event */
		ret = k_event_wait(&pdc_data[port]->port.sm_event,
				   PDC_PUBLIC_CMD_COMPLETE_EVENT, false,
				   K_MSEC(PUBLIC_CMD_DELAY_MS));

		if (ret != 0) {
			k_event_clear(&pdc_data[port]->port.sm_event,
				      PDC_PUBLIC_CMD_COMPLETE_EVENT);
		}

		if (sys_timepoint_expired(cmd_timepoint)) {
			/* something went wrong */
			LOG_ERR("C%d: Public API blocking timeout: %s", port,
				pdc_cmd_names[public_cmd->cmd]);
			public_cmd->pending = false;
			return -EBUSY;
		}

		/* Check for commands that don't require a connection */
		if (is_connectionless_cmd(public_cmd->cmd)) {
			continue;
		}

		/* The system is blocking on a command that requires a
		 * connection, so return if disconnected */
		if (!pdc_power_mgmt_is_connected(port)) {
			LOG_ERR("C%d: Command %s requires connection", port,
				pdc_cmd_names[public_cmd->cmd]);
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

	return pdc_data[port]->port.attached_state == SNK_ATTACHED_STATE;
}

static bool pdc_power_mgmt_is_source_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_state == SRC_ATTACHED_STATE;
}

test_mockable bool pdc_power_mgmt_is_connected(int port)
{
	if (!is_pdc_port_valid(port)) {
		return false;
	}

	return pdc_data[port]->port.attached_state != UNATTACHED_STATE;
}

uint8_t pdc_power_mgmt_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

int pdc_power_mgmt_set_active_charge_port(int charge_port)
{
	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable all ports */
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			pdc_data[i]->port.active_charge = false;
			atomic_set_bit(pdc_data[i]->port.snk_policy.flags,
				       SNK_POLICY_SET_ACTIVE_CHARGE_PORT);
		}
	} else if (is_pdc_port_valid(charge_port)) {
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			if (i == charge_port) {
				pdc_data[i]->port.active_charge = true;
			} else {
				pdc_data[i]->port.active_charge = false;
			}
			atomic_set_bit(pdc_data[i]->port.snk_policy.flags,
				       SNK_POLICY_SET_ACTIVE_CHARGE_PORT);
		}
	}

	return EC_SUCCESS;
}

int pdc_power_mgmt_set_new_power_request(int port)
{
	/* Make sure port is sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return -ENOTCONN;
	}

	atomic_set_bit(pdc_data[port]->port.snk_policy.flags,
		       SNK_POLICY_NEW_POWER_REQUEST);

	return EC_SUCCESS;
}

uint8_t pdc_power_mgmt_get_task_state(int port)
{
	enum pdc_state_t indicated_state, actual_state;

	if (!is_pdc_port_valid(port)) {
		return PDC_UNATTACHED;
	}

	actual_state = get_pdc_state(&pdc_data[port]->port);

	switch (actual_state) {
	case PDC_SEND_CMD_START:
	case PDC_SEND_CMD_WAIT:
		indicated_state = pdc_data[port]->port.send_cmd_return_state;
		break;
	default:
		indicated_state = actual_state;
	}

	return indicated_state;
}

int pdc_power_mgmt_comm_is_enabled(int port)
{
	if (pdc_power_mgmt_is_sink_connected(port) ||
	    pdc_power_mgmt_is_source_connected(port)) {
		return true;
	}

	return false;
}

bool pdc_power_mgmt_get_vconn_state(int port)
{
	bool vconn_sourcing;

	/* Make sure port is source connected */
	if (!pdc_power_mgmt_is_source_connected(port)) {
		return false;
	}

	pdc_data[port]->port.public_api_buff = (uint8_t *)&vconn_sourcing;

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_IS_VCONN_SOURCING)) {
		/* something went wrong */
		pdc_data[port]->port.public_api_buff = NULL;
		return false;
	}

	pdc_data[port]->port.public_api_buff = NULL;

	return vconn_sourcing;
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

test_mockable void pdc_power_mgmt_request_data_swap(int port)
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

test_mockable void pdc_power_mgmt_request_power_swap(int port)
{
	if (pdc_power_mgmt_is_sink_connected(port)) {
		pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SOURCE);
	} else if (pdc_power_mgmt_is_source_connected(port)) {
		pdc_power_mgmt_request_power_swap_intern(port, PD_ROLE_SINK);
	}
}

test_mockable enum tcpc_cc_polarity pdc_power_mgmt_pd_get_polarity(int port)
{
	if (pdc_data[port]->port.connector_status.orientation) {
		return POLARITY_CC2;
	}

	return POLARITY_CC1;
}

test_mockable enum pd_data_role pdc_power_mgmt_pd_get_data_role(int port)
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

test_mockable enum pd_power_role pdc_power_mgmt_get_power_role(int port)
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

	return (pdc_data[port]->port.attached_state == SNK_ATTACHED_STATE) ||
	       (pdc_data[port]->port.attached_state == SRC_ATTACHED_STATE);
}

bool pdc_power_mgmt_get_partner_dual_role_power(int port)
{
	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	return pdc_data[port]->port.ccaps.op_mode_drp;
}

test_mockable bool pdc_power_mgmt_get_partner_data_swap_capable(int port)
{
	struct pdc_port_t *pdc_port;
	uint32_t fixed_vsafe5v_pdo;

	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return false;
	}

	pdc_port = &pdc_data[port]->port;

	fixed_vsafe5v_pdo =
		get_pdc_pdos_ptr(pdc_port, &pdc_port->get_pdo)->pdos[0];

	/*
	 * Error check that first PDO is fixed, as 6.4.1 Capabilities requires
	 * in the Power Delivery Specification.
	 * "The vSafe5V Fixed Supply Object Shall always be the first object"
	 */
	if ((fixed_vsafe5v_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return false;

	return fixed_vsafe5v_pdo & PDO_FIXED_DATA_SWAP;
}

int pdc_power_mgmt_get_vbus_voltage(int port)
{
	struct pdc_port_t *port_data;

	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 0;
	}

	port_data = &pdc_data[port]->port;

	if (sys_timepoint_expired(port_data->vbus_expired)) {
		/* Block until command completes */
		if (public_api_block(port, CMD_PDC_GET_VBUS_VOLTAGE)) {
			/* something went wrong */
			return 0;
		}

		port_data->vbus_expired =
			sys_timepoint_calc(K_MSEC(VBUS_READ_CACHE_MS));
	}

	/* Return VBUS */
	return pdc_data[port]->port.vbus;
}

test_mockable int pdc_power_mgmt_reset(int port)
{
	int rv;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	/* Instruct the PDC driver to reset itself. This resets the driver to
	 * its initial state and re-runs the PDC setup routine commands.
	 */
	rv = public_api_block(port, CMD_PDC_RESET);
	if (rv) {
		return rv;
	}

	/* Revert back to init state */
	set_pdc_state(&pdc_data[port]->port, PDC_INIT);

	return 0;
}

test_mockable uint8_t pdc_power_mgmt_get_src_cap_cnt(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return 0;
	}

	return pdc_data[port]->port.snk_policy.src.pdo_count;
}

test_mockable const uint32_t *const pdc_power_mgmt_get_src_caps(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_sink_connected(port)) {
		return NULL;
	}

	return (const uint32_t *const)pdc_data[port]->port.snk_policy.src.pdos;
}

test_mockable const char *pdc_power_mgmt_get_task_state_name(int port)
{
	enum pdc_state_t state = pdc_power_mgmt_get_task_state(port);

	return pdc_state_names[state];
}

test_mockable void pdc_power_mgmt_set_dual_role(int port,
						enum pd_dual_role_states state)
{
	struct pdc_port_t *port_data = &pdc_data[port]->port;

	LOG_INF("C%d: pdc_power_mgmt_set_dual_role: set role to %d", port,
		state);

	/*
	 * clears the flags set in this function in case multiple consecutive
	 * calls to set_dual_role is made to prevent multiple roles being
	 * active at the same time.
	 */
	atomic_clear_bit(port_data->src_policy.flags, SRC_POLICY_FORCE_SNK);
	atomic_clear_bit(port_data->src_policy.flags, SRC_POLICY_SWAP_TO_SNK);
	atomic_clear_bit(port_data->snk_policy.flags, SNK_POLICY_SWAP_TO_SRC);

	switch (state) {
	/* While disconnected, toggle between src and sink */
	case PD_DRP_TOGGLE_ON:
		/* Allow external power role swaps */
		port_data->src_policy.accept_power_role_swap = true;
		port_data->snk_policy.accept_power_role_swap = true;

		port_data->una_policy.cc_mode = CCOM_DRP;
		atomic_set_bit(port_data->una_policy.flags, UNA_POLICY_CC_MODE);
		break;
	/* Stay in src until disconnect, then stay in sink forever */
	case PD_DRP_TOGGLE_OFF:
		/* Allow external power role swap from source to sink, but not
		 * the reverse */
		port_data->src_policy.accept_power_role_swap = true;
		port_data->snk_policy.accept_power_role_swap = false;

		port_data->una_policy.cc_mode = CCOM_RD;
		atomic_set_bit(port_data->una_policy.flags, UNA_POLICY_CC_MODE);
		break;
	/* Stay in current power role, don't switch. No auto-toggle support */
	case PD_DRP_FREEZE:
		/* No external power role swaps accepted */
		port_data->src_policy.accept_power_role_swap = false;
		port_data->snk_policy.accept_power_role_swap = false;

		if (pdc_power_mgmt_is_source_connected(port)) {
			port_data->una_policy.cc_mode = CCOM_RP;
		} else {
			port_data->una_policy.cc_mode = CCOM_RD;
		}
		atomic_set_bit(port_data->una_policy.flags, UNA_POLICY_CC_MODE);
		break;
	/* Switch to sink */
	case PD_DRP_FORCE_SINK:
		/* Allow external power role swap from src to sink */
		port_data->src_policy.accept_power_role_swap = true;
		port_data->snk_policy.accept_power_role_swap = false;

		if (pdc_power_mgmt_is_source_connected(port)) {
			port_data->pdr.swap_to_src = 0;
			port_data->pdr.swap_to_snk = 1;
			atomic_set_bit(port_data->src_policy.flags,
				       SRC_POLICY_SWAP_TO_SNK);
		}

		/*
		 * If PRS to Sink fails, or if not connected via PD, disconnect
		 * and reconnect as Sink.
		 */
		port_data->una_policy.cc_mode = CCOM_RD;
		atomic_set_bit(port_data->una_policy.flags, UNA_POLICY_CC_MODE);
		atomic_set_bit(port_data->src_policy.flags,
			       SRC_POLICY_FORCE_SNK);
		break;
	/* Switch to source */
	case PD_DRP_FORCE_SOURCE:
		/* Allow external power role swap from sink to src */
		port_data->src_policy.accept_power_role_swap = false;
		port_data->snk_policy.accept_power_role_swap = true;

		if (pdc_power_mgmt_is_sink_connected(port)) {
			port_data->pdr.swap_to_src = 1;
			port_data->pdr.swap_to_snk = 0;
			atomic_set_bit(port_data->snk_policy.flags,
				       SNK_POLICY_SWAP_TO_SRC);
		}
		break;
	}

	/* Trigger updates to the power role swap allow bit */
	atomic_set_bit(port_data->src_policy.flags,
		       SRC_POLICY_UPDATE_ALLOW_PR_SWAP);
	atomic_set_bit(port_data->snk_policy.flags,
		       SNK_POLICY_UPDATE_ALLOW_PR_SWAP);

	port_data->dual_role_state = state;
}

test_mockable enum pd_dual_role_states pdc_power_mgmt_get_dual_role(int port)
{
	struct pdc_port_t *port_data = &pdc_data[port]->port;

	return port_data->dual_role_state;
}

test_mockable int pdc_power_mgmt_set_trysrc(int port, bool enable)
{
	LOG_INF("PD setting TrySrc=%d", enable);

	pdc_data[port]->port.drp = (enable ? DRP_TRY_SRC : DRP_NORMAL);

	return public_api_block(port, CMD_PDC_SET_DRP);
}

static void set_hpd_wake_watch(int port)
{
	struct pdc_port_t *port_data = &pdc_data[port]->port;

	/* Only watch for HPD wake when connected to a DP Alt Mode partner with
	 * HPD_LVL low.
	 */
	port_data->hpd_wake_watch = false;
	if (!pdc_power_mgmt_pd_capable(port) ||
	    !(port_data->connector_status.conn_partner_flags &
	      CONNECTOR_PARTNER_FLAG_ALTERNATE_MODE) ||
	    PD_VDO_DPSTS_HPD_LVL(port_data->vdo_dp_status)) {
		return;
	}

	port_data->hpd_wake_watch = true;
}

static void clear_hpd_wake_watch(int port)
{
	struct pdc_port_t *port_data = &pdc_data[port]->port;
	port_data->hpd_wake_watch = false;
}

/**
 * PDC Chipset state Policies
 */

static void pd_chipset_resume(void)
{
	k_work_reschedule(&pdc_apply_power_state_policy_work,
			  PDC_POWER_STATE_DEBOUNCE_S);

	LOG_INF("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

static void pd_chipset_suspend(void)
{
	k_work_reschedule(&pdc_apply_power_state_policy_work,
			  PDC_POWER_STATE_DEBOUNCE_S);

	LOG_INF("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

static void pd_chipset_startup(void)
{
	k_work_reschedule(&pdc_apply_power_state_policy_work,
			  PDC_POWER_STATE_DEBOUNCE_S);

	LOG_INF("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

static void pd_chipset_shutdown(void)
{
	k_work_reschedule(&pdc_apply_power_state_policy_work,
			  PDC_POWER_STATE_DEBOUNCE_S);

	LOG_INF("PD:S3->S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pd_chipset_shutdown, HOOK_PRIO_DEFAULT);

test_mockable int pdc_power_mgmt_get_info(int port, struct pdc_info_t *pdc_info,
					  bool live)
{
	int ret;

	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (pdc_info == NULL) {
		return -EINVAL;
	}

	if (live) {
		/* Caller wants live chip info. Set up a public API call to
		 * retrieve it from the PDC.
		 */
		ret = public_api_block(port, CMD_PDC_GET_INFO);
		if (ret) {
			return ret;
		}

		/* Provide a copy of the current info struct to avoid exposing
		 * internal data structs.
		 */
		memcpy(pdc_info, &pdc_data[port]->port.info,
		       sizeof(struct pdc_info_t));
		return 0;
	}

	/* Non-live requests can be handled synchronously by calling directly
	 * into the PDC driver.
	 */
	return pdc_get_info(pdc_data[port]->port.pdc, pdc_info, false);
}

test_mockable int pdc_power_mgmt_get_lpm_ppm_info(int port,
						  struct lpm_ppm_info_t *info)
{
	int ret;

	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (info == NULL) {
		return -EINVAL;
	}

	pdc_data[port]->port.lpm_ppm_info = info;

	ret = public_api_block(port, CMD_PDC_GET_LPM_PPM_INFO);
	if (ret) {
		return ret;
	}

	return 0;
}

int pdc_power_mgmt_get_bus_info(int port, struct pdc_bus_info_t *pdc_bus_info)
{
	/* This operation is handled synchronously within the driver based on
	 * compile-time data. No need to block or go through the state machine.
	 */

	return pdc_get_bus_info(pdc_data[port]->port.pdc, pdc_bus_info);
}

int pdc_power_mgmt_get_rev(int port, enum tcpci_msg_type type)
{
	uint32_t rev;

	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 0;
	}

	switch (type) {
	case TCPCI_MSG_SOP:
		rev = pdc_data[port]->port.ccaps.partner_pd_revision - 1;
		break;
	case TCPCI_MSG_SOP_PRIME:
		rev = pdc_data[port]->port.cable_prop.cable_pd_revision - 1;
		break;
	default:
		rev = 0;
	}

	return rev;
}

const uint32_t *const pdc_power_mgmt_get_snk_caps(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_source_connected(port)) {
		return NULL;
	}

	return (const uint32_t *const)pdc_data[port]->port.src_policy.snk.pdos;
}

uint8_t pdc_power_mgmt_get_snk_cap_cnt(int port)
{
	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_source_connected(port)) {
		return 0;
	}

	return pdc_data[port]->port.src_policy.snk.pdo_count;
}

struct rmdo pdc_power_mgmt_get_partner_rmdo(int port)
{
	struct rmdo value = { 0 };

	/* The PD 3.1 Get_Revision Message is optional and currently not
	 * supported in the PDC, although this may change in future updates. */

	return value;
}

enum pd_discovery_state
pdc_power_mgmt_get_identity_discovery(int port, enum tcpci_msg_type type)
{
	enum pdc_cmd_t cmd;
	int ret;

	/* Make sure port is Sink connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return PD_DISC_NEEDED;
	}

	switch (type) {
	case TCPCI_MSG_SOP:
		cmd = CMD_PDC_GET_IDENTITY_DISCOVERY;
		break;
	case TCPCI_MSG_SOP_PRIME:
		return (pdc_data[port]->port.cable_prop.cable_type &&
			pdc_data[port]->port.cable_prop.mode_support) ?
			       PD_DISC_COMPLETE :
			       PD_DISC_FAIL;
	default:
		return PD_DISC_FAIL;
	}

	/* Block until command completes */
	ret = public_api_block(port, cmd);
	if (ret) {
		return PD_DISC_NEEDED;
	}

	return pdc_data[port]->port.discovery_state ? PD_DISC_COMPLETE :
						      PD_DISC_FAIL;
}

test_mockable int
pdc_power_mgmt_connector_reset(int port, enum connector_reset reset_type)
{
	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	/* Make sure port is connected */
	if (!pdc_power_mgmt_is_connected(port)) {
		return EC_SUCCESS;
	}

	pdc_data[port]->port.connector_reset.raw_value = 0;
	pdc_data[port]->port.connector_reset.reset_type = reset_type;

	/* Block until command completes */
	return public_api_block(port, CMD_PDC_CONNECTOR_RESET);
}

static int pdc_run_get_discovery(int port)
{
	int ret;

	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	/* Make sure port is connected and PD capable */
	if (!pdc_power_mgmt_is_connected(port) ||
	    !pdc_power_mgmt_pd_capable(port)) {
		return 0;
	}

	/* Format the GET_VDO command */
	discovery_info_init(&pdc_data[port]->port);

	/* Block until command completes */
	ret = public_api_block(port, CMD_PDC_GET_VDO);
	if (ret) {
		return ret;
	}

	LOG_INF("GET_VDO[%d]: vid = %04x, pid = %04x, prod_type = %d", port,
		PD_IDH_VID(pdc_data[port]->port.vdo[0]),
		PD_PRODUCT_PID(pdc_data[port]->port.vdo[1]),
		PD_IDH_PTYPE(pdc_data[port]->port.vdo[0]));

	return 0;
}

uint16_t pdc_power_mgmt_get_identity_vid(int port)
{
	uint16_t vid = 0;
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return vid;
	}

	pdc = &pdc_data[port]->port;
	/*
	 * TODO(b/327283662); GET_VDO completes with 0 length bytes to read
	 *
	 * The VDOs should be retrieved as part of either the src_attached or
	 * snk_attached state flows. However, if the port is connected during an
	 * EC reboot, then the GET_VDO command will complete successfully, but
	 * indicates a 0 VDO length and so the ST_READ state is skipped in the
	 * driver. Adding a work-around here such that if the first VDO is all
	 * 0s, then trigger another GET_VDO command in order to get the values
	 * required. GET_VDO is only sent, if the port is connected and pd
	 * capable.
	 *
	 */
	if (pdc->vdo[IDENTITY_VID_VDO_IDX] == INVALID_VDO_VALUE) {
		pdc_run_get_discovery(port);
	}

	if (pdc->vdo[IDENTITY_VID_VDO_IDX]) {
		vid = PD_IDH_VID(pdc->vdo[IDENTITY_VID_VDO_IDX]);
	}

	return vid;
}

uint16_t pdc_power_mgmt_get_identity_pid(int port)
{
	uint16_t pid = 0;
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return pid;
	}

	pdc = &pdc_data[port]->port;

	if (pdc->vdo[IDENTITY_VID_VDO_IDX] == INVALID_VDO_VALUE) {
		pdc_run_get_discovery(port);
	}

	if (pdc->vdo[IDENTITY_PID_VDO_IDX]) {
		pid = PD_PRODUCT_PID(pdc->vdo[IDENTITY_PID_VDO_IDX]);
	}

	return pid;
}

uint8_t pdc_power_mgmt_get_product_type(int port)
{
	uint8_t ptype = 0;
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return ptype;
	}

	pdc = &pdc_data[port]->port;

	if (pdc->vdo[IDENTITY_PTYPE_VDO_IDX] == INVALID_VDO_VALUE) {
		pdc_run_get_discovery(port);
	}

	if (pdc->vdo[IDENTITY_PTYPE_VDO_IDX]) {
		ptype = PD_IDH_VID(pdc->vdo[IDENTITY_PTYPE_VDO_IDX]);
	}

	return ptype;
}

/** Allow 3s for the PDC SM to suspend itself. */
#define SUSPEND_TIMEOUT_USEC (3 * USEC_PER_SEC)

/* TODO(b/323371550): This function should be adjusted to target individual PD
 * chips rather than all ports at once. It should take a chip ID as a param and
 * track current comms status by chip.
 */
test_mockable int pdc_power_mgmt_set_comms_state(bool enable_comms)
{
	int ret;
	int status = 0;
	static bool current_comms_status = true;

	if (enable_comms) {
		if (current_comms_status == true) {
			/* Comms are already enabled */
			return -EALREADY;
		}

		/* Resume and reset the driver layer */
		for (int p = 0; p < CONFIG_USB_PD_PORT_MAX_COUNT; p++) {
			ret = pdc_set_comms_state(pdc_data[p]->port.pdc, true);
			if (ret) {
				LOG_ERR("Cannot resume port C%d driver: %d", p,
					ret);
				status = ret;
			}
		}

		/* Release each PDC state machine. A reset is performed when
		 * exiting the suspended state.
		 */
		for (int p = 0; p < CONFIG_USB_PD_PORT_MAX_COUNT; p++) {
			atomic_set(&pdc_data[p]->port.suspend, 0);
		}

		if (status == 0) {
			/* Successfully re-enabled comms */
			current_comms_status = true;
		}
	} else {
		/* Disable/suspend communications */

		if (current_comms_status == false) {
			/* Comms are already disabled */
			return -EALREADY;
		}

		/* Request each port's PDC state machine to enter the suspend
		 * state.
		 */
		for (int p = 0; p < CONFIG_USB_PD_PORT_MAX_COUNT; p++) {
			atomic_set(&pdc_data[p]->port.suspend, 1);
		}

		/* Wait for each PDC state machine to enter suspended state */
		for (int p = 0; p < CONFIG_USB_PD_PORT_MAX_COUNT; p++) {
			ret = WAIT_FOR(get_pdc_state(&pdc_data[p]->port) ==
					       PDC_SUSPENDED,
				       SUSPEND_TIMEOUT_USEC,
				       k_sleep(K_MSEC(LOOP_DELAY_MS)));
			if (!ret) {
				LOG_ERR("Timed out suspending PDC SM for port "
					"C%d: %d",
					p, ret);
				status = -ETIMEDOUT;
			}
		}

		/* Suspend the driver layer */
		for (int p = 0; p < CONFIG_USB_PD_PORT_MAX_COUNT; p++) {
			ret = pdc_set_comms_state(pdc_data[p]->port.pdc, false);

			if (ret) {
				LOG_ERR("Cannot suspend port C%d driver: %d", p,
					ret);
				status = ret;
			}
		}

		if (status == 0) {
			/* Successfully disabled comms */
			current_comms_status = false;
		}
	}

	return status;
}

test_mockable int
pdc_power_mgmt_get_connector_status(int port,
				    union connector_status_t *connector_status)
{
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (connector_status == NULL) {
		return -EINVAL;
	}

	pdc = &pdc_data[port]->port;

	*connector_status = pdc->connector_status;

	return 0;
}

test_mockable int pdc_power_mgmt_get_last_status_change(
	int port, union conn_status_change_bits_t *status_change)
{
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (status_change == NULL) {
		return -EINVAL;
	}

	pdc = &pdc_data[port]->port;

	status_change->raw_value = pdc->ci.raw_value;

	return 0;
}

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
uint8_t pdc_power_mgmt_get_dp_pin_mode(int port)
{
	uint8_t pin_mode;

	/* Make sure port is in range and that an output buffer is provided */
	if (!is_pdc_port_valid(port)) {
		LOG_ERR("get_dp_pin_mode: invalid port %d", port);
		return 0;
	}

	/* Make sure port is connected and PD capable */
	if (!pdc_power_mgmt_is_connected(port)) {
		return 0;
	}

	/*
	 * Byte 1 (bits 15:8) contains the DP Source Device Pin assignment.
	 * The VDO pin assignments match our MODE_DP_PIN_x definitions.
	 */
	pin_mode = (pdc_data[port]->port.vdo_dp_cfg >> 8) & 0xFF;

	LOG_INF("C%d: DP pin mode 0x%02x", port, pin_mode);

	return pin_mode;
}
#endif

void pdc_power_mgmt_set_max_voltage(unsigned int mv)
{
	pdc_max_request_mv = mv;
}

test_mockable unsigned int pdc_power_mgmt_get_max_voltage(void)
{
	return pdc_max_request_mv;
}

test_mockable void pdc_power_mgmt_request_source_voltage(int port, int mv)
{
	pdc_power_mgmt_set_max_voltage(mv);

	if (pdc_power_mgmt_is_sink_connected(port)) {
		pdc_power_mgmt_set_new_power_request(port);
	} else {
		pdc_power_mgmt_request_swap_to_snk(port);
	}
}

test_mockable int
pdc_power_mgmt_get_cable_prop(int port, union cable_property_t *cable_prop)
{
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (cable_prop == NULL) {
		return -EINVAL;
	}

	*cable_prop = pdc_data[port]->port.cable_prop;

	return 0;
}

enum usb_typec_current_t pdc_power_mgmt_get_default_current_limit(int port)
{
	return TC_CURRENT_1_5A;
}

/**
 * @brief Adjust typec and USB-PD current limits
 */
int pdc_power_mgmt_set_current_limit(int port_num,
				     enum usb_typec_current_t current)
{
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port_num)) {
		return -ERANGE;
	}

	pdc = &pdc_data[port_num]->port;

	/* Always set the new Rp value */
	pdc->una_policy.tcc = current;

	/* Always set the new SRC PDO. */
	pdc->set_pdos.count = 1;
	pdc->set_pdos.type = SOURCE_PDO;
	pdc->set_pdos.pdos[0] = current == TC_CURRENT_3_0A ?
					pdc_src_pdo_max[0] :
					pdc_src_pdo_nominal[0];

	/* Further actions depend on the port attached state and power role */
	switch (pdc->attached_state) {
	case SRC_ATTACHED_TYPEC_ONLY_STATE:
		/*
		 * Active TypeC only SRC connection. Because the connection is
		 * active and not a PD connection, apply the new Rp value now.
		 */
		atomic_set_bit(pdc->src_policy.flags, SRC_POLICY_SET_RP);
		__fallthrough;
	case SRC_ATTACHED_STATE:
		/*
		 * Active USB-PD SRC connection. Update the LPM source cap which
		 * will also trigger the PDC to send a new SRC_CAP message to
		 * the port partner.
		 */

		/* Set flag to trigger SET_PDOS command to PDC */
		atomic_set_bit(pdc->src_policy.flags,
			       SRC_POLICY_UPDATE_SRC_CAPS);
		break;
	case SNK_ATTACHED_STATE:
		__fallthrough;
	case SNK_ATTACHED_TYPEC_ONLY_STATE:
		/* Even when operating as a SNK, update the SRC caps
		 * so that the first PDO offered after a power role
		 * swap is a safe value.
		 */
		atomic_set_bit(pdc->snk_policy.flags,
			       SNK_POLICY_UPDATE_SRC_CAPS);
		break;
	case UNATTACHED_STATE:
		/* Update the default Rp level */
		atomic_set_bit(pdc->una_policy.flags, UNA_POLICY_TCC);

		/* Set flag to trigger SET_PDOS command to PDC */
		atomic_set_bit(pdc->una_policy.flags,
			       UNA_POLICY_UPDATE_SRC_CAPS);
		break;
	}

	return EC_SUCCESS;
}

int pdc_power_mgmt_frs_enable(int port_num, bool enable)
{
	/*
	 * TODO(b/337958604): Currently there is no mechanism to enable/disable
	 * FRS. Waiting for this control to be available in PDC.
	 */

	return EC_SUCCESS;
}

int pdc_power_mgmt_get_pch_data_status(int port, uint8_t *status)
{
	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	if (status == NULL) {
		return -EINVAL;
	}

	/* Block until command completes */
	if (public_api_block(port, CMD_PDC_GET_PCH_DATA_STATUS)) {
		/* something went wrong */
		return -EIO;
	}

	memcpy(status, pdc_data[port]->port.pch_data_status, 5);
	return 0;
}

int pdc_power_mgmt_resync_port_state_for_ppm(int port)
{
	struct pdc_port_t *pdc;
	int rv;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	pdc = &pdc_data[port]->port;

	/* First clear the settle state event if it wasn't triggered for PPM. */
	k_event_clear(&pdc->sm_event, PDC_SM_SETTLED_EVENT);

	/* Trigger re-scan of connector status. */
	atomic_set_bit(pdc->cci_flags, CCI_EVENT);
	k_event_post(&pdc->sm_event, PDC_SM_EVENT);

	rv = k_event_wait(&pdc->sm_event, PDC_SM_SETTLED_EVENT, false,
			  K_MSEC(PDC_SM_SETTLED_TIMEOUT_MS));

	if (!rv) {
		return -ETIMEDOUT;
	}

	k_event_clear(&pdc->sm_event, rv);
	return 0;
}

int pdc_power_mgmt_ppm_ack_status_change(int port,
					 union conn_status_change_bits_t ci)
{
	struct pdc_port_t *pdc;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	pdc = &pdc_data[port]->port;

	pdc->overlay_ppm_changes.raw_value &= ~(ci.raw_value);
	pdc->connector_status.raw_conn_status_change_bits &= ~(ci.raw_value);

	return 0;
}

int pdc_power_mgmt_register_ppm_callback(const struct pdc_callback *callback)
{
	struct pdc_port_t *pdc;
	int port;

	for (port = 0; port < pdc_power_mgmt_get_usb_pd_port_count(); ++port) {
		pdc = &pdc_data[port]->port;
		pdc->ppm_ci_cb = callback;
	}

	return 0;
}

int pdc_power_mgmt_get_connector_status_for_ppm(
	int port, union connector_status_t *connector_status)
{
	struct pdc_port_t *pdc;
	int rv;

	if (!is_pdc_port_valid(port)) {
		return -ERANGE;
	}

	pdc = &pdc_data[port]->port;

	rv = pdc_power_mgmt_get_connector_status(port, connector_status);

	/* Overlay any additional connector status change bits we would like to
	 * add. This is necessary for the OPM to be made aware of role swaps and
	 * other methods causing connector status changes.
	 */
	if (rv == 0) {
		connector_status->raw_conn_status_change_bits |=
			pdc->overlay_ppm_changes.raw_value;
	}

	return rv;
}

#ifdef CONFIG_ZTEST

bool test_pdc_power_mgmt_is_snk_typec_attached_run(int port)
{
	LOG_INF("RPZ SRC %d",
		pdc_data[port]->port.snk_typec_attached_local_state);
	return pdc_data[port]->port.snk_typec_attached_local_state ==
	       SNK_TYPEC_ATTACHED_RUN;
}

bool test_pdc_power_mgmt_is_src_typec_attached_run(int port)
{
	LOG_INF("RPZ SRC %d",
		pdc_data[port]->port.src_typec_attached_local_state);
	return pdc_data[port]->port.src_typec_attached_local_state ==
	       SRC_TYPEC_ATTACHED_RUN;
}

/*
 * Reset the state machine for each port to its unattached state. This ensures
 * that tests start from the same state and prevents commands from a previous
 * test from impacting subsequently run tests.
 */
/* LCOV_EXCL_START */
bool pdc_power_mgmt_test_wait_unattached(void)
{
	int num_unattached;

	for (int port = 0; port < ARRAY_SIZE(pdc_data); port++) {
		set_pdc_state(&pdc_data[port]->port, PDC_UNATTACHED);
	}

	/* Wait for up to 20 * 100ms for all ports to become unattached. */
	for (int i = 0; i < 20; i++) {
		k_msleep(100);
		num_unattached = 0;

		for (int port = 0; port < ARRAY_SIZE(pdc_data); port++) {
			if (pdc_data[port]->port.unattached_local_state ==
			    UNATTACHED_RUN) {
				num_unattached++;
			}
		}

		if (num_unattached == ARRAY_SIZE(pdc_data)) {
			return true;
		}
	}

	return false;
}
/* LCOV_EXCL_STOP */

/*
 * Ensure that the PDC attached state is either SRC_ATTACHED or SNK_ATTACHED and
 * that the substate has reached the stead state for the attached state.
 */
/* LCOV_EXCL_START */
bool pdc_power_mgmt_is_pd_attached(int port)
{
	if ((pdc_data[port]->port.attached_state == SNK_ATTACHED_STATE) &&
	    (pdc_data[port]->port.snk_attached_local_state ==
	     SNK_ATTACHED_RUN)) {
		return true;
	}

	if ((pdc_data[port]->port.attached_state == SRC_ATTACHED_STATE) &&
	    (pdc_data[port]->port.src_attached_local_state ==
	     SRC_ATTACHED_RUN)) {
		return true;
	}

	return false;
}
/* LCOV_EXCL_STOP */

#endif
