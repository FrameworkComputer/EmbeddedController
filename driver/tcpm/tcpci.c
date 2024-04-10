/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "anx74xx.h"
#include "atomic.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "i2c.h"
#include "ps8xxx.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

STATIC_IF(CONFIG_USB_PD_DECODE_SOP)
bool sop_prime_en[CONFIG_USB_PD_PORT_MAX_COUNT];
STATIC_IF(CONFIG_USB_PD_DECODE_SOP)
int rx_en[CONFIG_USB_PD_PORT_MAX_COUNT];

#define TCPC_FLAGS_VSAFE0V(_flags)             \
	((_flags & TCPC_FLAGS_TCPCI_REV2_0) && \
	 !(_flags & TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V))

/****************************************************************************
 * TCPCI DEBUG Helpers
 */

/* TCPCI FAULT-0x01 is an invalid I2C operation was performed.  This tends
 * to have to do with the state of registers and the last write operation.
 * Defining DEBUG_I2C_FAULT_LAST_WRITE_OP will track the write operations,
 * excluding XFER and BlockWrites, in an attempt to give clues as to what
 * was written to the TCPCI that caused the issue.
 */
#undef DEBUG_I2C_FAULT_LAST_WRITE_OP

struct i2c_wrt_op {
	int addr;
	int reg;
	int val;
	int mask;
};
STATIC_IF(DEBUG_I2C_FAULT_LAST_WRITE_OP)
struct i2c_wrt_op last_write_op[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * AutoDischargeDisconnect has caused a number of issues with the
 * feature not being correctly enabled/disabled.  Defining
 * DEBUG_AUTO_DISCHARGE_DISCONNECT will output a line for each enable
 * and disable to help better understand any AutoDischargeDisconnect
 * issues.
 */
#undef DEBUG_AUTO_DISCHARGE_DISCONNECT

/*
 * ForcedDischarge debug to help coordinate with AutoDischarge.
 * Defining DEBUG_FORCED_DISCHARGE will output a line for each enable
 * and disable to help better understand any Discharge issues.
 */
#undef DEBUG_FORCED_DISCHARGE

/*
 * Seeing the CC Status and ROLE Control registers as well as the
 * CC that is being determined from this information can be
 * helpful.  Defining DEBUG_GET_CC will output a line that gives
 * this useful information
 */
#undef DEBUG_GET_CC

struct get_cc_values {
	int cc1;
	int cc2;
	int cc_sts;
	int role;
};
STATIC_IF(DEBUG_GET_CC)
struct get_cc_values last_get_cc[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * Seeing RoleCtrl updates can help determine why GetCC is not
 * working as it should be.
 */
#undef DEBUG_ROLE_CTRL_UPDATES

/****************************************************************************/

/*
 * Last reported VBus Level
 *
 * BIT(VBUS_SAFE0V) will indicate if in SAFE0V
 * BIT(VBUS_PRESENT) will indicate if in PRESENT in the TCPCI POWER_STATUS
 *
 * Note that VBUS_REMOVED cannot be distinguished from !VBUS_PRESENT with
 * this interface, but the trigger thresholds for Vbus Present should allow the
 * same bit to be used safely for both.
 *
 * TODO(b/149530538): Some TCPCs may be able to implement
 * VBUS_SINK_DISCONNECT_THRESHOLD to support vSinkDisconnectPD
 */
static int tcpc_vbus[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Cached RP role values */
static int cached_rp[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Cache our Device Capabilities at init for later reference */
static int dev_cap_1[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
int tcpc_addr_write(int port, int i2c_addr, int reg, int val)
{
	int rv;

	pd_wait_exit_low_power(port);

	if (IS_ENABLED(DEBUG_I2C_FAULT_LAST_WRITE_OP)) {
		last_write_op[port].addr = i2c_addr;
		last_write_op[port].reg = reg;
		last_write_op[port].val = val & 0xFF;
		last_write_op[port].mask = 0;
	}

	rv = i2c_write8(tcpc_config[port].i2c_info.port, i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_addr_write16(int port, int i2c_addr, int reg, int val)
{
	int rv;

	pd_wait_exit_low_power(port);

	if (IS_ENABLED(DEBUG_I2C_FAULT_LAST_WRITE_OP)) {
		last_write_op[port].addr = i2c_addr;
		last_write_op[port].reg = reg;
		last_write_op[port].val = val & 0xFFFF;
		last_write_op[port].mask = 0;
	}

	rv = i2c_write16(tcpc_config[port].i2c_info.port, i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_addr_read(int port, int i2c_addr, int reg, int *val)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_read8(tcpc_config[port].i2c_info.port, i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_addr_read16(int port, int i2c_addr, int reg, int *val)
{
	pd_wait_exit_low_power(port);

	return tcpc_addr_read16_no_lpm_exit(port, i2c_addr, reg, val);
}

int tcpc_addr_read16_no_lpm_exit(int port, int i2c_addr, int reg, int *val)
{
	int rv;

	rv = i2c_read16(tcpc_config[port].i2c_info.port, i2c_addr, reg, val);

	pd_device_accessed(port);
	return rv;
}

int tcpc_read_block(int port, int reg, uint8_t *in, int size)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_read_block(tcpc_config[port].i2c_info.port,
			    tcpc_config[port].i2c_info.addr_flags, reg, in,
			    size);

	pd_device_accessed(port);
	return rv;
}

int tcpc_write_block(int port, int reg, const uint8_t *out, int size)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_write_block(tcpc_config[port].i2c_info.port,
			     tcpc_config[port].i2c_info.addr_flags, reg, out,
			     size);

	pd_device_accessed(port);
	return rv;
}

int tcpc_xfer(int port, const uint8_t *out, int out_size, uint8_t *in,
	      int in_size)
{
	int rv;
	/* Dispatching to tcpc_xfer_unlocked reduces code size growth. */
	tcpc_lock(port, 1);
	rv = tcpc_xfer_unlocked(port, out, out_size, in, in_size,
				I2C_XFER_SINGLE);
	tcpc_lock(port, 0);
	return rv;
}

int tcpc_xfer_unlocked(int port, const uint8_t *out, int out_size, uint8_t *in,
		       int in_size, int flags)
{
	int rv;

	pd_wait_exit_low_power(port);

	rv = i2c_xfer_unlocked(tcpc_config[port].i2c_info.port,
			       tcpc_config[port].i2c_info.addr_flags, out,
			       out_size, in, in_size, flags);

	pd_device_accessed(port);
	return rv;
}

int tcpc_update8(int port, int reg, uint8_t mask,
		 enum mask_update_action action)
{
	int rv;
	const int i2c_addr = tcpc_config[port].i2c_info.addr_flags;

	pd_wait_exit_low_power(port);

	if (IS_ENABLED(DEBUG_I2C_FAULT_LAST_WRITE_OP)) {
		last_write_op[port].addr = i2c_addr;
		last_write_op[port].reg = reg;
		last_write_op[port].val = 0;
		last_write_op[port].mask = (mask & 0xFF) | (action << 16);
	}

	rv = i2c_update8(tcpc_config[port].i2c_info.port, i2c_addr, reg, mask,
			 action);

	pd_device_accessed(port);
	return rv;
}

int tcpc_update16(int port, int reg, uint16_t mask,
		  enum mask_update_action action)
{
	int rv;
	const int i2c_addr = tcpc_config[port].i2c_info.addr_flags;

	pd_wait_exit_low_power(port);

	if (IS_ENABLED(DEBUG_I2C_FAULT_LAST_WRITE_OP)) {
		last_write_op[port].addr = i2c_addr;
		last_write_op[port].reg = reg;
		last_write_op[port].val = 0;
		last_write_op[port].mask = (mask & 0xFFFF) | (action << 16);
	}

	rv = i2c_update16(tcpc_config[port].i2c_info.port, i2c_addr, reg, mask,
			  action);

	pd_device_accessed(port);
	return rv;
}

#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

/*
 * TCPCI maintains and uses cached values for the RP and
 * last used PULL values.  Since TCPC drivers are allowed
 * to use some of the TCPCI functionality, these global
 * cached values need to be maintained in case part of the
 * used TCPCI functionality relies on these values
 */
void tcpci_set_cached_rp(int port, int rp)
{
	cached_rp[port] = rp;
}

int tcpci_get_cached_rp(int port)
{
	return cached_rp[port];
}

static int init_alert_mask(int port)
{
	int rv;
	uint16_t mask;

	/*
	 * Create mask of alert events that will cause the TCPC to
	 * signal the TCPM via the Alert# gpio line.
	 */
	if (get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_TCPC) {
		mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		       TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		       TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS |
		       TCPC_REG_ALERT_FAULT | TCPC_REG_ALERT_POWER_STATUS;
	} else {
		mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		       TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		       TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS |
		       TCPC_REG_ALERT_FAULT;
	}

	/* TCPCI Rev2 includes SAFE0V alerts */
	if (TCPC_FLAGS_VSAFE0V(tcpc_config[port].flags))
		mask |= TCPC_REG_ALERT_EXT_STATUS;

	if (tcpm_tcpc_has_frs_control(port))
		mask |= TCPC_REG_ALERT_ALERT_EXT;

	/* Set the alert mask in TCPC */
	rv = tcpc_write16(port, TCPC_REG_ALERT_MASK, mask);

	if (tcpm_tcpc_has_frs_control(port)) {
		if (rv)
			return rv;

		/* Sink FRS allowed */
		mask = TCPC_REG_ALERT_EXT_SNK_FRS;
		rv = tcpc_write(port, TCPC_REG_ALERT_EXTENDED_MASK, mask);
	}
	return rv;
}

static int clear_alert_mask(int port)
{
	return tcpc_write16(port, TCPC_REG_ALERT_MASK, 0);
}

static int init_power_status_mask(int port)
{
	uint8_t mask;
	int rv;

	if (get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_TCPC)
		mask = TCPC_REG_POWER_STATUS_VBUS_PRES;
	else
		mask = 0;

	rv = tcpc_write(port, TCPC_REG_POWER_STATUS_MASK, mask);

	return rv;
}

static int clear_power_status_mask(int port)
{
	return tcpc_write(port, TCPC_REG_POWER_STATUS_MASK, 0);
}

static int tcpci_tcpm_get_power_status(int port, int *status)
{
	return tcpc_read(port, TCPC_REG_POWER_STATUS, status);
}

int tcpci_tcpm_select_rp_value(int port, int rp)
{
	/* Keep track of current RP value */
	tcpci_set_cached_rp(port, rp);

	return EC_SUCCESS;
}

void tcpci_tcpc_discharge_vbus(int port, int enable)
{
	if (IS_ENABLED(DEBUG_FORCED_DISCHARGE))
		CPRINTS("C%d: ForceDischarge %sABLED", port,
			enable ? "EN" : "DIS");

	tcpc_update8(port, TCPC_REG_POWER_CTRL,
		     TCPC_REG_POWER_CTRL_FORCE_DISCHARGE,
		     (enable) ? MASK_SET : MASK_CLR);
}

/*
 * Auto Discharge Disconnect is supposed to be enabled when we
 * are connected and disabled after we are disconnected and
 * VBus is at SafeV0
 */
void tcpci_tcpc_enable_auto_discharge_disconnect(int port, int enable)
{
	if (IS_ENABLED(DEBUG_AUTO_DISCHARGE_DISCONNECT))
		CPRINTS("C%d: AutoDischargeDisconnect %sABLED", port,
			enable ? "EN" : "DIS");

	tcpc_update8(port, TCPC_REG_POWER_CTRL,
		     TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT,
		     (enable) ? MASK_SET : MASK_CLR);
}

int tcpci_tcpc_debug_accessory(int port, bool enable)
{
	return tcpc_update8(port, TCPC_REG_CONFIG_STD_OUTPUT,
			    TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N,
			    enable ? MASK_CLR : MASK_SET);
}

int tcpci_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
		      enum tcpc_cc_voltage_status *cc2)
{
	int role;
	int status;
	int cc1_present_rd, cc2_present_rd;
	int rv;

	/* errors will return CC as open */
	*cc1 = TYPEC_CC_VOLT_OPEN;
	*cc2 = TYPEC_CC_VOLT_OPEN;

	/* Get the ROLE CONTROL and CC STATUS values */
	rv = tcpc_read(port, TCPC_REG_ROLE_CTRL, &role);
	if (rv)
		return rv;

	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);
	if (rv)
		return rv;

	/* Get the current CC values from the CC STATUS */
	*cc1 = TCPC_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_REG_CC_STATUS_CC2(status);

	/* Determine if we are presenting Rd */
	cc1_present_rd = 0;
	cc2_present_rd = 0;
	if (role & TCPC_REG_ROLE_CTRL_DRP_MASK) {
		/*
		 * We are doing DRP.  We will use the CC STATUS
		 * ConnectResult to determine if we are presenting
		 * Rd or Rp.
		 */
		int term;

		term = TCPC_REG_CC_STATUS_TERM(status);

		if (*cc1 != TYPEC_CC_VOLT_OPEN)
			cc1_present_rd = term;
		if (*cc2 != TYPEC_CC_VOLT_OPEN)
			cc2_present_rd = term;
	} else {
		/*
		 * We are not doing DRP.  We will use the ROLE CONTROL
		 * CC values to determine if we are presenting Rd or Rp.
		 */
		int role_cc1, role_cc2;

		role_cc1 = TCPC_REG_ROLE_CTRL_CC1(role);
		role_cc2 = TCPC_REG_ROLE_CTRL_CC2(role);

		if (*cc1 != TYPEC_CC_VOLT_OPEN)
			cc1_present_rd = !!(role_cc1 == TYPEC_CC_RD);
		if (*cc2 != TYPEC_CC_VOLT_OPEN)
			cc2_present_rd = !!(role_cc2 == TYPEC_CC_RD);
	}
	*cc1 |= cc1_present_rd << 2;
	*cc2 |= cc2_present_rd << 2;

	if (IS_ENABLED(DEBUG_GET_CC) &&
	    (last_get_cc[port].cc1 != *cc1 || last_get_cc[port].cc2 != *cc2 ||
	     last_get_cc[port].cc_sts != status ||
	     last_get_cc[port].role != role)) {
		CPRINTS("C%d: GET_CC cc1=%d cc2=%d cc_sts=0x%X role=0x%X", port,
			*cc1, *cc2, status, role);

		last_get_cc[port].cc1 = *cc1;
		last_get_cc[port].cc2 = *cc2;
		last_get_cc[port].cc_sts = status;
		last_get_cc[port].role = role;
	}
	return rv;
}

int tcpci_tcpm_set_cc(int port, int pull)
{
	int role = TCPC_REG_ROLE_CTRL_SET(
		TYPEC_NO_DRP, tcpci_get_cached_rp(port), pull, pull);

	if (IS_ENABLED(DEBUG_ROLE_CTRL_UPDATES))
		CPRINTS("C%d: SET_CC pull=%d role=0x%X", port, pull, role);

	return tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
int tcpci_set_role_ctrl(int port, enum tcpc_drp drp, enum tcpc_rp_value rp,
			enum tcpc_cc_pull pull)
{
	int role = TCPC_REG_ROLE_CTRL_SET(drp, rp, pull, pull);

	if (IS_ENABLED(DEBUG_ROLE_CTRL_UPDATES))
		CPRINTS("C%d: SET_ROLE_CTRL drp=%d rp=%d pull=%d role=0x%X",
			port, drp, rp, pull, role);

	return tcpc_write(port, TCPC_REG_ROLE_CTRL, role);
}

int tcpci_tcpc_drp_toggle(int port)
{
	int rv;
	enum tcpc_cc_pull pull;

	/*
	 * Set auto drp toggle
	 *
	 *     Set RC.DRP=1b (DRP)
	 *     Set RC.RpValue=00b (smallest Rp to save power)
	 *     Set RC.CC1=(Rp) or (Rd)
	 *     Set RC.CC2=(Rp) or (Rd)
	 *
	 * TCPCI r1 wants both lines to be set to Rd
	 * TCPCI r2 wants both lines to be set to Rp
	 *
	 * Set the Rp Value to be the minimal to save power
	 */
	pull = (tcpc_config[port].flags & TCPC_FLAGS_TCPCI_REV2_0) ?
		       TYPEC_CC_RP :
		       TYPEC_CC_RD;

	rv = tcpci_set_role_ctrl(port, TYPEC_DRP, TYPEC_RP_USB, pull);
	if (rv)
		return rv;

	/* Set up to catch LOOK4CONNECTION alerts */
	rv = tcpc_update8(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT,
			  MASK_SET);
	if (rv)
		return rv;

	/* Set Look4Connection command */
	rv = tcpc_write(port, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_LOOK4CONNECTION);

	return rv;
}
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
int tcpci_enter_low_power_mode(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

void tcpci_wake_low_power_mode(int port)
{
	/*
	 * TCPCI 4.8.1 I2C Interface - wake the TCPC with a throw-away command
	 *
	 * TODO(b/205140007): Align LPM exit to TCPCI spec for TCPCs which can
	 * correctly support it
	 */
	i2c_write8(tcpc_config[port].i2c_info.port,
		   tcpc_config[port].i2c_info.addr_flags, TCPC_REG_COMMAND,
		   TCPC_REG_COMMAND_WAKE_I2C);
}
#endif

int tcpci_tcpm_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	return tcpc_update8(port, TCPC_REG_TCPC_CTRL, TCPC_REG_TCPC_CTRL_SET(1),
			    polarity_rm_dts(polarity) ? MASK_SET : MASK_CLR);
}

bool tcpci_tcpm_get_snk_ctrl(int port)
{
	int rv;
	int pwr_sts;

	rv = tcpci_tcpm_get_power_status(port, &pwr_sts);

	return rv == EC_SUCCESS && pwr_sts & TCPC_REG_POWER_STATUS_SINKING_VBUS;
}

int tcpci_tcpm_set_snk_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SNK_CTRL_HIGH :
			   TCPC_REG_COMMAND_SNK_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}

bool tcpci_tcpm_get_src_ctrl(int port)
{
	int rv;
	int pwr_sts;

	rv = tcpci_tcpm_get_power_status(port, &pwr_sts);

	return rv == EC_SUCCESS &&
	       pwr_sts & TCPC_REG_POWER_STATUS_SOURCING_VBUS;
}

int tcpci_tcpm_set_src_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SRC_CTRL_HIGH :
			   TCPC_REG_COMMAND_SRC_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}

__maybe_unused int tcpci_tcpm_sop_prime_enable(int port, bool enable)
{
	/* save SOP'/SOP'' enable state */
	sop_prime_en[port] = enable;

	if (rx_en[port]) {
		int detect_sop_en = TCPC_REG_RX_DETECT_SOP_HRST_MASK;

		if (enable) {
			detect_sop_en =
				TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK;
		}

		return tcpc_write(port, TCPC_REG_RX_DETECT, detect_sop_en);
	}

	return EC_SUCCESS;
}

int tcpci_tcpm_set_vconn(int port, int enable)
{
	int reg, rv;

	rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &reg);
	if (rv)
		return rv;

	reg &= ~TCPC_REG_POWER_CTRL_VCONN(1);
	reg |= TCPC_REG_POWER_CTRL_VCONN(enable);

	return tcpc_write(port, TCPC_REG_POWER_CTRL, reg);
}

int tcpci_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_write(port, TCPC_REG_MSG_HDR_INFO,
			  TCPC_REG_MSG_HDR_INFO_SET(data_role, power_role));
}

static int tcpm_alert_status(int port, int *alert)
{
	/* Read TCPC Alert register */
	return tcpc_read16(port, TCPC_REG_ALERT, alert);
}

static int tcpm_alert_ext_status(int port, int *alert_ext)
{
	/* Read TCPC Extended Alert register */
	return tcpc_read(port, TCPC_REG_ALERT_EXT, alert_ext);
}

static int tcpm_ext_status(int port, int *ext_status)
{
	/* Read TCPC Extended Status register */
	return tcpc_read(port, TCPC_REG_EXT_STATUS, ext_status);
}

int tcpci_tcpm_set_rx_enable(int port, int enable)
{
	int detect_sop_en = 0;

	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
		/* save rx_on */
		rx_en[port] = enable;
	}

	if (enable) {
		detect_sop_en = TCPC_REG_RX_DETECT_SOP_HRST_MASK;

		if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) &&
		    sop_prime_en[port]) {
			/*
			 * Only the VCONN Source is allowed to communicate
			 * with the Cable Plugs.
			 */
			detect_sop_en =
				TCPC_REG_RX_DETECT_SOP_SOPP_SOPPP_HRST_MASK;
		}
	}

	/* If enable, then set RX detect for SOP and HRST */
	return tcpc_write(port, TCPC_REG_RX_DETECT, detect_sop_en);
}

#ifdef CONFIG_USB_PD_FRS
int tcpci_tcpc_fast_role_swap_enable(int port, int enable)
{
	return tcpc_update8(port, TCPC_REG_POWER_CTRL,
			    TCPC_REG_POWER_CTRL_FRS_ENABLE,
			    (enable) ? MASK_SET : MASK_CLR);
}
#endif

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
bool tcpci_tcpm_check_vbus_level(int port, enum vbus_level level)
{
	if (level == VBUS_SAFE0V)
		return !!(tcpc_vbus[port] & BIT(VBUS_SAFE0V));
	else if (level == VBUS_PRESENT)
		return !!(tcpc_vbus[port] & BIT(VBUS_PRESENT));
	else
		return !(tcpc_vbus[port] & BIT(VBUS_PRESENT));
}
#endif

struct cached_tcpm_message {
	uint32_t header;
	uint32_t payload[7];
};

static int tcpci_rev2_0_tcpm_get_message_raw(int port, uint32_t *payload,
					     int *head)
{
	int rv = 0, cnt, reg = TCPC_REG_RX_BUFFER;
	int frm;
	uint8_t tmp[2];
	/*
	 * Register 0x30 is Readable Byte Count, Buffer frame type, and RX buf
	 * byte X.
	 */
	tcpc_lock(port, 1);
	rv = tcpc_xfer_unlocked(port, (uint8_t *)&reg, 1, tmp, 2,
				I2C_XFER_START);
	if (rv) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}
	cnt = tmp[0];
	frm = tmp[1];

	/*
	 * READABLE_BYTE_COUNT includes 3 bytes for frame type and header, and
	 * may be 0 if the TCPC saw a disconnect before the message read
	 */
	cnt -= 3;
	if ((cnt < 0) ||
	    (cnt > member_size(struct cached_tcpm_message, payload))) {
		/* Continue to send the stop bit with the header read */
		rv = EC_ERROR_UNKNOWN;
		cnt = 0;
	}

	/* The next two bytes are the header */
	rv |= tcpc_xfer_unlocked(port, NULL, 0, (uint8_t *)head, 2,
				 cnt ? 0 : I2C_XFER_STOP);

	/* Encode message address in bits 31 to 28 */
	*head &= 0x0000ffff;
	*head |= PD_HEADER_SOP(frm);

	/* Execute read and I2C_XFER_STOP, even if header read failed */
	if (cnt > 0) {
		tcpc_xfer_unlocked(port, NULL, 0, (uint8_t *)payload, cnt,
				   I2C_XFER_STOP);
	}

clear:
	tcpc_lock(port, 0);
	/* Read complete, clear RX status alert bit */
	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_STATUS);

	if (rv)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int tcpci_rev1_0_tcpm_get_message_raw(int port, uint32_t *payload,
					     int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;
	int frm;

	rv = tcpc_read(port, TCPC_REG_RX_BYTE_CNT, &cnt);

	/* RX_BYTE_CNT includes 3 bytes for frame type and header */
	if (rv != EC_SUCCESS || cnt < 3) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}
	cnt -= 3;
	if (cnt > member_size(struct cached_tcpm_message, payload)) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}

	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
		rv = tcpc_read(port, TCPC_REG_RX_BUF_FRAME_TYPE, &frm);
		if (rv != EC_SUCCESS) {
			rv = EC_ERROR_UNKNOWN;
			goto clear;
		}
	}

	rv = tcpc_read16(port, TCPC_REG_RX_HDR, (int *)head);

	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
		/* Encode message address in bits 31 to 28 */
		*head &= 0x0000ffff;
		*head |= PD_HEADER_SOP(frm);
	}

	if (rv == EC_SUCCESS && cnt > 0) {
		tcpc_read_block(port, reg, (uint8_t *)payload, cnt);
	}

clear:
	/* Read complete, clear RX status alert bit */
	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_STATUS);

	return rv;
}

int tcpci_tcpm_get_message_raw(int port, uint32_t *payload, int *head)
{
	if (tcpc_config[port].flags & TCPC_FLAGS_TCPCI_REV2_0)
		return tcpci_rev2_0_tcpm_get_message_raw(port, payload, head);

	return tcpci_rev1_0_tcpm_get_message_raw(port, payload, head);
}

/* Cache depth needs to be power of 2 */
/* TODO: Keep track of the high water mark */
#define CACHE_DEPTH BIT(3)
#define CACHE_DEPTH_MASK (CACHE_DEPTH - 1)

struct queue {
	/*
	 * Head points to the index of the first empty slot to put a new RX
	 * message. Must be masked before used in lookup.
	 */
	atomic_t head;
	/*
	 * Tail points to the index of the first message for the PD task to
	 * consume. Must be masked before used in lookup.
	 */
	atomic_t tail;
	struct cached_tcpm_message buffer[CACHE_DEPTH];
};
static struct queue cached_messages[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Note this method can be called from an interrupt context. */
int tcpm_enqueue_message(const int port)
{
	int rv;
	struct queue *const q = &cached_messages[port];
	struct cached_tcpm_message *const head =
		&q->buffer[q->head & CACHE_DEPTH_MASK];

	if (q->head - q->tail == CACHE_DEPTH) {
		CPRINTS("C%d RX EC Buffer full!", port);
		return EC_ERROR_OVERFLOW;
	}

	/* Blank any old message, just in case. */
	memset(head, 0, sizeof(*head));
	/* Call the raw driver without caching */
	rv = tcpc_config[port].drv->get_message_raw(port, head->payload,
						    &head->header);
	if (rv) {
		CPRINTS("C%d: Could not retrieve RX message (%d)", port, rv);
		return rv;
	}

	/* Increment atomically to ensure get_message_raw happens-before */
	atomic_add(&q->head, 1);

	/* Wake PD task up so it can process incoming RX messages */
	task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE);

	return EC_SUCCESS;
}

int tcpm_has_pending_message(const int port)
{
	const struct queue *const q = &cached_messages[port];

	return q->head != q->tail;
}

int tcpm_dequeue_message(const int port, uint32_t *const payload,
			 int *const header)
{
	struct queue *const q = &cached_messages[port];
	struct cached_tcpm_message *const tail =
		&q->buffer[q->tail & CACHE_DEPTH_MASK];

	if (!tcpm_has_pending_message(port)) {
		CPRINTS("C%d No message in RX buffer!", port);
		return EC_ERROR_BUSY;
	}

	/* Copy cache data in to parameters */
	*header = tail->header;
	memcpy(payload, tail->payload, sizeof(tail->payload));

	/* Increment atomically to ensure memcpy happens-before */
	atomic_add(&q->tail, 1);

	return EC_SUCCESS;
}

void tcpm_clear_pending_messages(int port)
{
	struct queue *const q = &cached_messages[port];

	q->tail = q->head;
}

int tcpci_tcpm_transmit(int port, enum tcpci_msg_type type, uint16_t header,
			const uint32_t *data)
{
	int reg = TCPC_REG_TX_DATA;
	int rv, cnt = 4 * PD_HEADER_CNT(header);

	/* If not SOP* transmission, just write to the transmit register */
	if (type >= NUM_SOP_STAR_TYPES) {
		/*
		 * Per TCPCI spec, do not specify retry (although the TCPC
		 * should ignore retry field for these 3 types).
		 */
		return tcpc_write(port, TCPC_REG_TRANSMIT,
				  TCPC_REG_TRANSMIT_SET_WITHOUT_RETRY(type));
	}

	if (tcpc_config[port].flags & TCPC_FLAGS_TCPCI_REV2_0) {
		/*
		 * In TCPCI Rev 2.0, TX_BYTE_CNT and TX_BUF_BYTE_X are the same
		 * register.
		 */
		reg = TCPC_REG_TX_BUFFER;
		/* TX_BYTE_CNT includes extra bytes for message header */
		cnt += sizeof(header);
		tcpc_lock(port, 1);
		rv = tcpc_xfer_unlocked(port, (uint8_t *)&reg, 1, NULL, 0,
					I2C_XFER_START);
		rv |= tcpc_xfer_unlocked(port, (uint8_t *)&cnt, 1, NULL, 0, 0);
		if (cnt > sizeof(header)) {
			rv |= tcpc_xfer_unlocked(port, (uint8_t *)&header,
						 sizeof(header), NULL, 0, 0);
			rv |= tcpc_xfer_unlocked(port, (uint8_t *)data,
						 cnt - sizeof(header), NULL, 0,
						 I2C_XFER_STOP);
		} else {
			rv |= tcpc_xfer_unlocked(port, (uint8_t *)&header,
						 sizeof(header), NULL, 0,
						 I2C_XFER_STOP);
		}
		tcpc_lock(port, 0);

		/* If tcpc write fails, return error */
		if (rv)
			return rv;
	} else {
		/* TX_BYTE_CNT includes extra bytes for message header */
		rv = tcpc_write(port, TCPC_REG_TX_BYTE_CNT,
				cnt + sizeof(header));

		rv |= tcpc_write16(port, TCPC_REG_TX_HDR, header);

		/* If tcpc write fails, return error */
		if (rv)
			return rv;

		if (cnt > 0) {
			rv = tcpc_write_block(port, reg, (const uint8_t *)data,
					      cnt);

			/* If tcpc write fails, return error */
			if (rv)
				return rv;
		}
	}

	/*
	 * The PRL_RX state machine should force a discard of PRL_TX any time a
	 * new message comes in.  However, since most of the PRL_RX runs on
	 * the TCPC, we may receive a RX interrupt between the EC PRL_RX and
	 * PRL_TX state machines running.  In this case, mark the message
	 * discarded and don't tell the TCPC to transmit.
	 */
	if (tcpm_has_pending_message(port)) {
		pd_transmit_complete(port, TCPC_TX_COMPLETE_DISCARDED);
		return EC_ERROR_BUSY;
	}

	/*
	 * We always retry in TCPC hardware since the TCPM is too slow to
	 * respond within tRetry (~195 usec).
	 *
	 * The retry count used is dependent on the maximum PD revision
	 * supported at build time.
	 */
	return tcpc_write(port, TCPC_REG_TRANSMIT,
			  TCPC_REG_TRANSMIT_SET_WITH_RETRY(
				  pd_get_retry_count(port, type), type));
}

/*
 * Returns true if TCPC has reset based on reading mask registers.
 */
static int register_mask_reset(int port)
{
	int mask;

	mask = 0;
	tcpc_read16(port, TCPC_REG_ALERT_MASK, &mask);
	if (mask == TCPC_REG_ALERT_MASK_ALL)
		return 1;

	mask = 0;
	tcpc_read(port, TCPC_REG_POWER_STATUS_MASK, &mask);
	if (mask == TCPC_REG_POWER_STATUS_MASK_ALL)
		return 1;

	return 0;
}

static int tcpci_get_fault(int port, int *fault)
{
	return tcpc_read(port, TCPC_REG_FAULT_STATUS, fault);
}

static int tcpci_handle_fault(int port, int fault)
{
	int rv = EC_SUCCESS;

	CPRINTS("C%d FAULT 0x%02X detected", port, fault);

	if (IS_ENABLED(DEBUG_I2C_FAULT_LAST_WRITE_OP) &&
	    fault & TCPC_REG_FAULT_STATUS_I2C_INTERFACE_ERR) {
		if (last_write_op[port].mask == 0)
			CPRINTS("C%d I2C WR 0x%02X 0x%02X value=0x%X", port,
				last_write_op[port].addr,
				last_write_op[port].reg,
				last_write_op[port].val);
		else
			CPRINTS("C%d I2C UP 0x%02X 0x%02X op=%d mask=0x%X",
				port, last_write_op[port].addr,
				last_write_op[port].reg,
				last_write_op[port].mask >> 16,
				last_write_op[port].mask & 0xFFFF);
	}

	/* Report overcurrent to the OCP module if enabled */
	if ((dev_cap_1[port] & TCPC_REG_DEV_CAP_1_VBUS_OCP_REPORTING) &&
	    IS_ENABLED(CONFIG_USBC_OCP) &&
	    (fault & TCPC_REG_FAULT_STATUS_VBUS_OVER_CURRENT))
		pd_handle_overcurrent(port);

	if (tcpc_config[port].drv->handle_fault)
		rv = tcpc_config[port].drv->handle_fault(port, fault);

	return rv;
}

int tcpci_hard_reset_reinit(int port)
{
	int rv;

	/* Initialize power_status_mask */
	rv = init_power_status_mask(port);
	/* Initialize alert_mask */
	rv |= init_alert_mask(port);

	CPRINTS("C%d: Hard Reset re-initialize %s", port,
		rv ? "failed" : "success");

	return rv;
}

enum ec_error_list tcpci_set_bist_test_mode(const int port, const bool enable)
{
	int rv;

	rv = tcpc_update8(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_BIST_TEST_MODE,
			  enable ? MASK_SET : MASK_CLR);
	rv |= tcpc_update16(port, TCPC_REG_ALERT_MASK, TCPC_REG_ALERT_RX_STATUS,
			    enable ? MASK_CLR : MASK_SET);
	return rv;
}

enum ec_error_list tcpci_get_bist_test_mode(const int port, bool *enable)
{
	int rv;
	int val;

	rv = tcpc_read(port, TCPC_REG_TCPC_CTRL, &val);
	*enable = !!(val & TCPC_REG_TCPC_CTRL_BIST_TEST_MODE);

	return rv;
}

static int tcpci_clear_fault(int port, int fault)
{
	int rv;

	rv = tcpc_write(port, TCPC_REG_FAULT_STATUS, fault);
	if (rv)
		return rv;

	return tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_FAULT);
}

static void tcpci_check_vbus_changed(int port, int alert, uint32_t *pd_event)
{
	/*
	 * Check for VBus change
	 */
	/* TCPCI Rev2 includes Safe0V detection */
	if (TCPC_FLAGS_VSAFE0V(tcpc_config[port].flags) &&
	    (alert & TCPC_REG_ALERT_EXT_STATUS)) {
		int ext_status = 0;

		/* Determine if Safe0V was detected */
		tcpm_ext_status(port, &ext_status);
		if (ext_status & TCPC_REG_EXT_STATUS_SAFE0V)
			/* Safe0V=1 and Present=0 */
			tcpc_vbus[port] = BIT(VBUS_SAFE0V);
	}

	if (alert & TCPC_REG_ALERT_POWER_STATUS) {
		int pwr_status = 0;

		/* Determine reason for power status change */
		tcpci_tcpm_get_power_status(port, &pwr_status);
		if (pwr_status & TCPC_REG_POWER_STATUS_VBUS_PRES)
			/* Safe0V=0 and Present=1 */
			tcpc_vbus[port] = BIT(VBUS_PRESENT);
		else if (TCPC_FLAGS_VSAFE0V(tcpc_config[port].flags))
			/* TCPCI Rev2 detects Safe0V, so Present=0 */
			tcpc_vbus[port] &= ~BIT(VBUS_PRESENT);
		else {
			/*
			 * TCPCI Rev1 can not detect Safe0V, so treat this
			 * like a Safe0V detection.
			 *
			 * Safe0V=1 and Present=0
			 */
			tcpc_vbus[port] = BIT(VBUS_SAFE0V);
		}

		if (get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_TCPC) {
			/* Update charge manager with new VBUS state */
			if (IS_ENABLED(CONFIG_USB_CHARGER))
				usb_charger_vbus_change(port,
							!!(tcpc_vbus[port] &
							   BIT(VBUS_PRESENT)));

			if (pd_event) {
				*pd_event |= TASK_EVENT_WAKE;
			}
		}
	}
}

/*
 * Don't let the TCPC try to pull from the RX buffer forever. We typical only
 * have 1 or 2 messages waiting.
 */
#define MAX_ALLOW_FAILED_RX_READS 10

void tcpci_tcpc_alert(int port)
{
	int alert = 0;
	int alert_ext = 0;
	int failed_attempts;
	uint32_t pd_event = 0;
	int retval = 0;
	bool bist_mode;

	/* Read the Alert register from the TCPC */
	if (tcpm_alert_status(port, &alert)) {
		CPRINTS("C%d: Failed to read alert register", port);
		return;
	}

	/* Get Extended Alert register if needed */
	if (alert & TCPC_REG_ALERT_ALERT_EXT)
		tcpm_alert_ext_status(port, &alert_ext);

	/* Clear any pending faults */
	if (alert & TCPC_REG_ALERT_FAULT) {
		int fault;

		if (tcpci_get_fault(port, &fault) == EC_SUCCESS && fault != 0 &&
		    tcpci_handle_fault(port, fault) == EC_SUCCESS &&
		    tcpci_clear_fault(port, fault) == EC_SUCCESS)
			CPRINTS("C%d FAULT 0x%02X handled", port, fault);
	}

	/*
	 * Check for TX complete first b/c PD state machine waits on TX
	 * completion events. This will send an event to the PD tasks
	 * immediately
	 */
	if (alert & TCPC_REG_ALERT_TX_COMPLETE) {
		int tx_status;

		if (alert & TCPC_REG_ALERT_TX_SUCCESS)
			tx_status = TCPC_TX_COMPLETE_SUCCESS;
		else if (alert & TCPC_REG_ALERT_TX_DISCARDED)
			tx_status = TCPC_TX_COMPLETE_DISCARDED;
		else
			tx_status = TCPC_TX_COMPLETE_FAILED;

		pd_transmit_complete(port, tx_status);
	}

	tcpc_get_bist_test_mode(port, &bist_mode);

	/* Pull all RX messages from TCPC into EC memory */
	failed_attempts = 0;
	while (alert & TCPC_REG_ALERT_RX_STATUS) {
		/*
		 * Some TCPCs do not properly disable interrupts during BIST
		 * test mode. For reducing I2C access time,
		 * there is no need to read out BIST data, just break.
		 * (see b/229812911).
		 */
		if (bist_mode)
			break;

		retval = tcpm_enqueue_message(port);
		if (retval)
			++failed_attempts;
		if (tcpm_alert_status(port, &alert))
			++failed_attempts;

		/*
		 * EC RX FIFO is full. Deassert ALERT# line to exit interrupt
		 * handler by discarding pending message from TCPC RX FIFO.
		 */
		if (retval == EC_ERROR_OVERFLOW) {
			CPRINTS("C%d: PD RX OVF!", port);
			tcpc_write16(port, TCPC_REG_ALERT,
				     TCPC_REG_ALERT_RX_STATUS |
					     TCPC_REG_ALERT_RX_BUF_OVF);
		}

		/* Ensure we don't loop endlessly */
		if (failed_attempts >= MAX_ALLOW_FAILED_RX_READS) {
			CPRINTS("C%d Cannot consume RX buffer after %d failed attempts!",
				port, failed_attempts);
			/*
			 * The port is in a bad state, we don't want to consume
			 * all EC resources so suspend the port for a little
			 * while.
			 */
			pd_set_suspend(port, 1);
			pd_deferred_resume(port);
			return;
		}
	}

	/*
	 * Clear all pending alert bits. Ext first because ALERT.AlertExtended
	 * is set if any bit of ALERT_EXTENDED is set.
	 */
	if (alert_ext)
		tcpc_write(port, TCPC_REG_ALERT_EXT, alert_ext);
	if (alert)
		tcpc_write16(port, TCPC_REG_ALERT, alert);

	if (alert & TCPC_REG_ALERT_CC_STATUS) {
		if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE)) {
			enum tcpc_cc_voltage_status cc1;
			enum tcpc_cc_voltage_status cc2;

			/*
			 * Some TCPCs generate CC Alerts when
			 * drp auto toggle is active and nothing
			 * is connected to the port. So, get the
			 * CC line status and only generate a
			 * PD_EVENT_CC if something is connected.
			 */
			tcpci_tcpm_get_cc(port, &cc1, &cc2);
			if (cc1 != TYPEC_CC_VOLT_OPEN ||
			    cc2 != TYPEC_CC_VOLT_OPEN)
				/* CC status cchanged, wake task */
				pd_event |= PD_EVENT_CC;
		} else {
			/* CC status changed, wake task */
			pd_event |= PD_EVENT_CC;
		}
	}

	tcpci_check_vbus_changed(port, alert, &pd_event);

	/* Check for Hard Reset received */
	if (alert & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		CPRINTS("C%d Hard Reset received", port);

		tcpm_hard_reset_reinit(port);

		pd_event |= PD_EVENT_RX_HARD_RESET;
	}

	/* USB TCPCI Spec R2 V1.1 Section 4.7.3 Step 2
	 *
	 * The TCPC asserts both ALERT.TransmitSOP*MessageSuccessful and
	 * ALERT.TransmitSOP*MessageFailed regardless of the outcome of the
	 * transmission and asserts the Alert# pin.
	 */
	if (alert & TCPC_REG_ALERT_TX_SUCCESS &&
	    alert & TCPC_REG_ALERT_TX_FAILED)
		CPRINTS("C%d Hard Reset sent", port);

	if (tcpm_tcpc_has_frs_control(port) &&
	    (alert_ext & TCPC_REG_ALERT_EXT_SNK_FRS))
		pd_got_frs_signal(port);

	/*
	 * Check registers to see if we can tell that the TCPC has reset. If
	 * so, perform a tcpc_init.
	 *
	 * Some TCPCs do not properly disable interrupts during BIST test mode.
	 * As TCPC not reset at this moment, no need to check pd reset status to
	 * reduce I2C access time.(see b/229812911)
	 */
	if (!bist_mode && register_mask_reset(port))
		pd_event |= PD_EVENT_TCPC_RESET;

	/*
	 * Wait until all possible TCPC accesses in this function are complete
	 * prior to setting events and/or waking the pd task. When the PD
	 * task is woken and runs (which will happen during I2C transactions in
	 * this function), the pd task may put the TCPC into low power mode and
	 * the next I2C transaction to the TCPC will cause it to wake again.
	 */
	if (pd_event)
		task_set_event(PD_PORT_TO_TASK_ID(port), pd_event);
}

test_mockable int tcpci_get_vbus_voltage_no_check(int port, int *vbus)
{
	int error, val;

	error = tcpc_read16(port, TCPC_REG_VBUS_VOLTAGE, &val);
	if (error)
		return error;

	*vbus = TCPC_REG_VBUS_VOLTAGE_VBUS(val);
	return EC_SUCCESS;
}

int tcpci_get_vbus_voltage(int port, int *vbus)
{
	if (!(dev_cap_1[port] & TCPC_REG_DEV_CAP_1_VBUS_MEASURE_ALARM_CAPABLE))
		return EC_ERROR_UNIMPLEMENTED;

	return tcpci_get_vbus_voltage_no_check(port, vbus);
}

int tcpci_get_chip_info_mutable(
	int port, int live, struct ec_response_pd_chip_info_v1 *const chip_info,
	int (*const mutator)(int port, bool live,
			     struct ec_response_pd_chip_info_v1 *cached))
{
	static struct ec_response_pd_chip_info_v1
		cached_info[CONFIG_USB_PD_PORT_MAX_COUNT];
	struct ec_response_pd_chip_info_v1 *info;
	int error;

	if (port >= board_get_usb_pd_port_count())
		return EC_ERROR_INVAL;

	info = &cached_info[port];

	/* Fetch live data if nothing is cached or live data was requested */
	if (!info->vendor_id || live) {
		int vendor_id, product_id, device_id;

		/*
		 * The cache is no longer valid because we're fetching. Avoid
		 * storing the new vendor ID until we've actually succeeded so
		 * future invocations won't see partial data and assume it's a
		 * valid cache.
		 */
		info->vendor_id = 0;
		error = tcpc_read16(port, TCPC_REG_VENDOR_ID, &vendor_id);
		if (error)
			return error;

		error = tcpc_read16(port, TCPC_REG_PRODUCT_ID, &product_id);
		if (error)
			return error;
		info->product_id = product_id;

		error = tcpc_read16(port, TCPC_REG_BCD_DEV, &device_id);
		if (error)
			return error;
		info->device_id = device_id;

		/*
		 * This varies chip to chip; more specific driver code is
		 * expected to override this value if it can by providing a
		 * mutator.
		 */
		info->fw_version_number = -1;

		info->vendor_id = vendor_id;
		if (mutator != NULL) {
			error = mutator(port, live, info);
			if (error) {
				/*
				 * Mutator needs to have a complete view, but if
				 * it fails the cache is invalidated.
				 */
				info->vendor_id = 0;
				return error;
			}
		}
	}
	/*
	 * If chip_info is NULL, this invocation will ensure the cache is fresh
	 * but return nothing.
	 */
	if (chip_info)
		memcpy(chip_info, info, sizeof(*info));

	return EC_SUCCESS;
}

int tcpci_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 *chip_info)
{
	return tcpci_get_chip_info_mutable(port, live, chip_info, NULL);
}

/*
 * Dissociate from the TCPC.
 */

int tcpci_tcpm_release(int port)
{
	int error;

	error = clear_alert_mask(port);
	if (error)
		return error;
	error = clear_power_status_mask(port);
	if (error)
		return error;
	/* Clear pending interrupts */
	error = tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
	if (error)
		return error;

	return EC_SUCCESS;
}

/*
 * On TCPC i2c failure, make 30 tries (at least 300ms) before giving up
 * in order to allow the TCPC time to boot / reset.
 */
#define TCPM_INIT_TRIES 30

int tcpci_tcpm_init(int port)
{
	int error;
	int power_status;
	int tries = TCPM_INIT_TRIES;

	if (port >= board_get_usb_pd_port_count())
		return EC_ERROR_INVAL;

	while (1) {
		error = tcpci_tcpm_get_power_status(port, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initialization is complete, clear all alert bits and write
		 * the initial alert mask.
		 */
		if (!error && !(power_status & TCPC_REG_POWER_STATUS_UNINIT))
			break;
		if (--tries <= 0)
			return error ? error : EC_ERROR_TIMEOUT;
		crec_msleep(10);
	}

	/*
	 * For TCPCI Rev 2.0, unless the TCPM sets
	 * TCPC_CONTROL.EnableLooking4ConnectionAlert bit, TCPC by default masks
	 * Alert assertion when CC_STATUS.Looking4Connection changes state.
	 */
	if (tcpc_config[port].flags & TCPC_FLAGS_TCPCI_REV2_0) {
		error = tcpc_update8(
			port, TCPC_REG_TCPC_CTRL,
			TCPC_REG_TCPC_CTRL_EN_LOOK4CONNECTION_ALERT, MASK_SET);
		if (error)
			CPRINTS("C%d: Failed to init TCPC_CTRL!", port);
	}

	/*
	 * Handle and clear any alerts, since we might be coming out of low
	 * power mode in response to an alert interrupt from the TCPC.
	 */
	tcpc_alert(port);
	/* Initialize power_status_mask */
	init_power_status_mask(port);

	if (TCPC_FLAGS_VSAFE0V(tcpc_config[port].flags)) {
		int ext_status = 0;

		/* Read Extended Status register */
		tcpm_ext_status(port, &ext_status);
		/* Initial level, set appropriately */
		if (power_status & TCPC_REG_POWER_STATUS_VBUS_PRES)
			tcpc_vbus[port] = BIT(VBUS_PRESENT);
		else if (ext_status & TCPC_REG_EXT_STATUS_SAFE0V)
			tcpc_vbus[port] = BIT(VBUS_SAFE0V);
		else
			tcpc_vbus[port] = 0;
	} else {
		/* Initial level, set appropriately */
		tcpc_vbus[port] =
			(power_status & TCPC_REG_POWER_STATUS_VBUS_PRES) ?
				BIT(VBUS_PRESENT) :
				BIT(VBUS_SAFE0V);
	}

	/* Enable/disable VBUS monitor by the flag */
	error = tcpc_update8(port, TCPC_REG_POWER_CTRL,
			     TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS,
			     tcpc_config[port].flags & TCPC_FLAGS_VBUS_MONITOR ?
				     MASK_CLR :
				     MASK_SET);
	if (error)
		return error;

	/*
	 * Force an update to the VBUS status in case the TCPC doesn't send a
	 * power status changed interrupt later.
	 */
	tcpci_check_vbus_changed(
		port, TCPC_REG_ALERT_POWER_STATUS | TCPC_REG_ALERT_EXT_STATUS,
		NULL);

	error = init_alert_mask(port);
	if (error)
		return error;

	/* Read chip info here when we know the chip is awake. */
	tcpm_get_chip_info(port, 1, NULL);

	/* Cache our device capabilities for future reference */
	tcpc_read16(port, TCPC_REG_DEV_CAP_1, &dev_cap_1[port]);

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPM_MUX

/*
 * When the TCPC/MUX device is only used for the MUX, we need to initialize it
 * via mux init because tcpc_init won't run for the device. This is borrowed
 * from tcpc_init.
 */
int tcpci_tcpm_mux_init(const struct usb_mux *me)
{
	int error;
	int power_status;
	int tries = TCPM_INIT_TRIES;

	/* If this MUX is also the TCPC, then skip init */
	if (!(me->flags & USB_MUX_FLAG_NOT_TCPC))
		return EC_SUCCESS;

	/* Wait for the device to exit low power state */
	while (1) {
		error = mux_read(me, TCPC_REG_POWER_STATUS, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initialization is complete.
		 */
		if (!error && !(power_status & TCPC_REG_POWER_STATUS_UNINIT))
			break;
		if (--tries <= 0)
			return error ? error : EC_ERROR_TIMEOUT;
		crec_msleep(10);
	}

	/* Turn off all alerts and acknowledge any pending IRQ */
	error = mux_write16(me, TCPC_REG_ALERT_MASK, 0);
	error |= mux_write16(me, TCPC_REG_ALERT, 0xffff);

	return error ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}

int tcpci_tcpm_mux_enter_low_power(const struct usb_mux *me)
{
	/* If this MUX is also the TCPC, then skip low power */
	if (!(me->flags & USB_MUX_FLAG_NOT_TCPC))
		return EC_SUCCESS;

	return mux_write(me, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

int tcpci_tcpm_mux_set(const struct usb_mux *me, mux_state_t mux_state,
		       bool *ack_required)
{
	int rv;
	int reg = 0;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	/* Parameter is port only */
	rv = mux_read(me, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	reg &= ~(TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK |
		 TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED);
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;

	/* Parameter is port only */
	return mux_write(me, TCPC_REG_CONFIG_STD_OUTPUT, reg);
}

/* Reads control register and updates mux_state accordingly */
int tcpci_tcpm_mux_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	int rv;
	int reg = 0;

	*mux_state = 0;

	/* Parameter is port only */
	rv = mux_read(me, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver tcpci_tcpm_usb_mux_driver = {
	.init = &tcpci_tcpm_mux_init,
	.set = &tcpci_tcpm_mux_set,
	.get = &tcpci_tcpm_mux_get,
	.enter_low_power_mode = &tcpci_tcpm_mux_enter_low_power,
};

#endif /* CONFIG_USB_PD_TCPM_MUX */

#ifdef CONFIG_CMD_TCPC_DUMP
static const struct tcpc_reg_dump_map tcpc_regs[] = {
	{
		.addr = TCPC_REG_VENDOR_ID,
		.name = "VENDOR_ID",
		.size = 2,
	},
	{
		.addr = TCPC_REG_PRODUCT_ID,
		.name = "PRODUCT_ID",
		.size = 2,
	},
	{
		.addr = TCPC_REG_BCD_DEV,
		.name = "BCD_DEV",
		.size = 2,
	},
	{
		.addr = TCPC_REG_TC_REV,
		.name = "TC_REV",
		.size = 2,
	},
	{
		.addr = TCPC_REG_PD_REV,
		.name = "PD_REV",
		.size = 2,
	},
	{
		.addr = TCPC_REG_PD_INT_REV,
		.name = "PD_INT_REV",
		.size = 2,
	},
	{
		.addr = TCPC_REG_ALERT,
		.name = "ALERT",
		.size = 2,
	},
	{
		.addr = TCPC_REG_ALERT_MASK,
		.name = "ALERT_MASK",
		.size = 2,
	},
	{
		.addr = TCPC_REG_POWER_STATUS_MASK,
		.name = "POWER_STATUS_MASK",
		.size = 1,
	},
	{
		.addr = TCPC_REG_FAULT_STATUS_MASK,
		.name = "FAULT_STATUS_MASK",
		.size = 1,
	},
	{ .addr = TCPC_REG_EXT_STATUS_MASK,
	  .name = "EXT_STATUS_MASK",
	  .size = 1 },
	{
		.addr = TCPC_REG_ALERT_EXTENDED_MASK,
		.name = "ALERT_EXTENDED_MASK",
		.size = 1,
	},
	{
		.addr = TCPC_REG_CONFIG_STD_OUTPUT,
		.name = "CONFIG_STD_OUTPUT",
		.size = 1,
	},
	{
		.addr = TCPC_REG_TCPC_CTRL,
		.name = "TCPC_CTRL",
		.size = 1,
	},
	{
		.addr = TCPC_REG_ROLE_CTRL,
		.name = "ROLE_CTRL",
		.size = 1,
	},
	{
		.addr = TCPC_REG_FAULT_CTRL,
		.name = "FAULT_CTRL",
		.size = 1,
	},
	{
		.addr = TCPC_REG_POWER_CTRL,
		.name = "POWER_CTRL",
		.size = 1,
	},
	{
		.addr = TCPC_REG_CC_STATUS,
		.name = "CC_STATUS",
		.size = 1,
	},
	{
		.addr = TCPC_REG_POWER_STATUS,
		.name = "POWER_STATUS",
		.size = 1,
	},
	{
		.addr = TCPC_REG_FAULT_STATUS,
		.name = "FAULT_STATUS",
		.size = 1,
	},
	{
		.addr = TCPC_REG_EXT_STATUS,
		.name = "EXT_STATUS",
		.size = 1,
	},
	{
		.addr = TCPC_REG_ALERT_EXT,
		.name = "ALERT_EXT",
		.size = 1,
	},
	{
		.addr = TCPC_REG_DEV_CAP_1,
		.name = "DEV_CAP_1",
		.size = 2,
	},
	{
		.addr = TCPC_REG_DEV_CAP_2,
		.name = "DEV_CAP_2",
		.size = 2,
	},
	{
		.addr = TCPC_REG_STD_INPUT_CAP,
		.name = "STD_INPUT_CAP",
		.size = 1,
	},
	{
		.addr = TCPC_REG_STD_OUTPUT_CAP,
		.name = "STD_OUTPUT_CAP",
		.size = 1,
	},
	{
		.addr = TCPC_REG_CONFIG_EXT_1,
		.name = "CONFIG_EXT_1",
		.size = 1,
	},
	{
		.addr = TCPC_REG_MSG_HDR_INFO,
		.name = "MSG_HDR_INFO",
		.size = 1,
	},
	{
		.addr = TCPC_REG_RX_DETECT,
		.name = "RX_DETECT",
		.size = 1,
	},
	{
		.addr = TCPC_REG_RX_BYTE_CNT,
		.name = "RX_BYTE_CNT",
		.size = 1,
	},
	{
		.addr = TCPC_REG_RX_BUF_FRAME_TYPE,
		.name = "RX_BUF_FRAME_TYPE",
		.size = 1,
	},
	{
		.addr = TCPC_REG_TRANSMIT,
		.name = "TRANSMIT",
		.size = 1,
	},
	{
		.addr = TCPC_REG_VBUS_VOLTAGE,
		.name = "VBUS_VOLTAGE",
		.size = 2,
	},
	{
		.addr = TCPC_REG_VBUS_SINK_DISCONNECT_THRESH,
		.name = "VBUS_SINK_DISCONNECT_THRESH",
		.size = 2,
	},
	{
		.addr = TCPC_REG_VBUS_STOP_DISCHARGE_THRESH,
		.name = "VBUS_STOP_DISCHARGE_THRESH",
		.size = 2,
	},
	{
		.addr = TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG,
		.name = "VBUS_VOLTAGE_ALARM_HI_CFG",
		.size = 2,
	},
	{
		.addr = TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG,
		.name = "VBUS_VOLTAGE_ALARM_LO_CFG",
		.size = 2,
	},
};

/*
 * Dump standard TCPC registers.
 */
test_mockable void tcpc_dump_std_registers(int port)
{
	tcpc_dump_registers(port, tcpc_regs, ARRAY_SIZE(tcpc_regs));
}
#endif

const struct tcpm_drv tcpci_tcpm_drv = {
	.init = &tcpci_tcpm_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.get_vbus_voltage = &tcpci_get_vbus_voltage,
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &tcpci_tcpm_set_cc,
	.set_polarity = &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &tcpci_tcpm_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
	.tcpc_enable_auto_discharge_disconnect =
		&tcpci_tcpc_enable_auto_discharge_disconnect,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
	.get_snk_ctrl = &tcpci_tcpm_get_snk_ctrl,
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.get_src_ctrl = &tcpci_tcpm_get_src_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &tcpci_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable = &tcpci_tcpc_fast_role_swap_enable,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers = &tcpc_dump_std_registers,
#endif
};
