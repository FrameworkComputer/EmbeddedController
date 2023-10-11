/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Richtek RT1739 USB-C Power Path Controller */
#include "atomic.h"
#include "battery.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "driver/ppc/rt1739.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "usbc_ppc.h"
#include "util.h"

#if defined(CONFIG_USBC_PPC_VCONN) && !defined(CONFIG_USBC_PPC_POLARITY)
#error "Can't use set_vconn without set_polarity"
#endif

static int rt1739_pd_connect_flag;
/* Check RT1739_FLAGS_*. */
static atomic_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

#ifdef TEST_BUILD
int rt1739_get_flag(int port)
{
	return flags[port];
}
#endif

static int read_reg(uint8_t port, int reg, int *val)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags, reg, val);
}

static int write_reg(uint8_t port, int reg, int val)
{
	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags, reg, val);
}

static int update_reg(int port, int reg, int mask,
		      enum mask_update_action action)
{
	return i2c_update8(ppc_chips[port].i2c_port,
			   ppc_chips[port].i2c_addr_flags, reg, mask, action);
}

static int rt1739_is_sourcing_vbus(int port)
{
	return flags[port] & RT1739_FLAGS_SOURCE_ENABLED;
}

static int rt1739_vbus_source_enable(int port, int enable)
{
	atomic_t prev_flag;

	if (enable)
		prev_flag =
			atomic_or(&flags[port], RT1739_FLAGS_SOURCE_ENABLED);
	else
		prev_flag = atomic_clear_bits(&flags[port],
					      RT1739_FLAGS_SOURCE_ENABLED);

	/* Return if status doesn't change */
	if (!!(prev_flag & RT1739_FLAGS_SOURCE_ENABLED) == !!enable)
		return EC_SUCCESS;

	RETURN_ERROR(update_reg(port, RT1739_REG_VBUS_SWITCH_CTRL,
				RT1739_LV_SRC_EN,
				enable ? MASK_SET : MASK_CLR));

#if defined(CONFIG_USB_CHARGER) && defined(CONFIG_USB_PD_VBUS_DETECT_PPC)
	/*
	 * Since the VBUS state could be changing here, need to wake the
	 * USB_CHG_N task so that BC 1.2 detection will be triggered.
	 */
	usb_charger_vbus_change(port, enable);
#endif

	return EC_SUCCESS;
}

static int rt1739_vbus_sink_enable(int port, int enable)
{
	return update_reg(port, RT1739_REG_VBUS_SWITCH_CTRL, RT1739_HV_SNK_EN,
			  enable ? MASK_SET : MASK_CLR);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int rt1739_dump(int port)
{
	ccprintf("    ");
	for (int i = 0; i < 16; i++) {
		ccprintf("%2X ", i);
	}
	ccprintf("\n");

	for (int i = 0; i <= 0x61; i++) {
		int val = 0;
		int rt = read_reg(port, i, &val);

		if (i % 16 == 0)
			ccprintf("%02X: ", i);
		if (rt)
			ccprintf("-- ");
		else
			ccprintf("%02X ", val);
		if (i % 16 == 15)
			ccprintf("\n");
	}
	ccprintf("\n");

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int rt1739_is_vbus_present(int port)
{
	__maybe_unused static atomic_t vbus_prev[CONFIG_USB_PD_PORT_MAX_COUNT];
	int status, vbus;

	if (read_reg(port, RT1739_REG_INT_STS4, &status))
		return 0;

	vbus = !!(status & RT1739_VBUS_PRESENT);

#ifdef CONFIG_USB_CHARGER
	if (!!(vbus_prev[port] != vbus))
		usb_charger_vbus_change(port, vbus);

	if (vbus)
		atomic_or(&vbus_prev[port], 1);
	else
		atomic_clear(&vbus_prev[port]);
#endif

	return vbus;
}
#endif

#ifdef CONFIG_USBC_PPC_POLARITY
static int rt1739_set_polarity(int port, int polarity)
{
	return update_reg(port, RT1739_REG_VCONN_CTRL1, RT1739_VCONN_ORIENT,
			  polarity ? RT1739_VCONN_ORIENT_CC1 :
				     RT1739_VCONN_ORIENT_CC2);
}
#endif

#ifdef CONFIG_USBC_PPC_VCONN
static int rt1739_set_vconn(int port, int enable)
{
	return update_reg(port, RT1739_REG_VCONN_CTRL1, RT1739_VCONN_EN,
			  enable ? MASK_SET : MASK_CLR);
}
#endif

static int rt1739_get_device_id(int port, int *device_id)
{
	return read_reg(port, RT1739_REG_DEVICE_ID0, device_id);
}

static int rt1739_workaround(int port)
{
	int device_id, vconn_ctrl4, lvhvsw_ov_ctrl;

	RETURN_ERROR(rt1739_get_device_id(port, &device_id));

	switch (device_id) {
	case RT1739_DEVICE_ID_ES1:
		CPRINTS("RT1739 ES1");
		RETURN_ERROR(update_reg(port, RT1739_REG_SYS_CTRL1,
					RT1739_OSC640K_FORCE_EN, MASK_SET));

		RETURN_ERROR(write_reg(port, RT1739_VBUS_FAULT_DIS,
				       RT1739_OVP_DISVBUS_EN |
					       RT1739_UVLO_DISVBUS_EN |
					       RT1739_SCP_DISVBUS_EN |
					       RT1739_OCPS_DISVBUS_EN));
		RETURN_ERROR(update_reg(port, RT1739_REG_VCONN_CTRL3,
					RT1739_VCONN_CLIMIT_EN, MASK_SET));
		break;

	case RT1739_DEVICE_ID_ES2:
		CPRINTS("RT1739 ES2");
		/* Disable SWENB test output */
		/* enter hidden mode */
		RETURN_ERROR(write_reg(port, 0xF1, 0x62));
		RETURN_ERROR(write_reg(port, 0xF0, 0x86));
		/* turn off SWENB output */
		RETURN_ERROR(write_reg(port, 0xE0, 0x07));
		/* leave hidden mode */
		RETURN_ERROR(write_reg(port, 0xF1, 0));
		RETURN_ERROR(write_reg(port, 0xF0, 0));

		/* Set VBUS to VIN_LV leakage remove setting */
		RETURN_ERROR(write_reg(port, RT1739_VBUS_FAULT_DIS, 0));
		RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_CTRL1, 0));
		RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_SWITCH_CTRL, 0));
		msleep(5);
		RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_SWITCH_CTRL,
				       RT1739_LV_SRC_EN));
		msleep(5);
		RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_SWITCH_CTRL, 0));
		msleep(5);
		RETURN_ERROR(write_reg(
			port, RT1739_VBUS_FAULT_DIS,
			RT1739_OVP_DISVBUS_EN | RT1739_UVLO_DISVBUS_EN |
				RT1739_RCP_DISVBUS_EN | RT1739_SCP_DISVBUS_EN));
		RETURN_ERROR(
			write_reg(port, RT1739_REG_VBUS_CTRL1,
				  RT1739_HVLV_SCP_EN | RT1739_HVLV_OCRC_EN));
		RETURN_ERROR(update_reg(port, RT1739_REG_VCONN_CTRL3,
					RT1739_VCONN_CLIMIT_EN, MASK_SET));
		break;

	case RT1739_DEVICE_ID_ES4:
		CPRINTS("RT1739 ES4");
		RETURN_ERROR(read_reg(port, RT1739_REG_LVHVSW_OV_CTRL,
				      &lvhvsw_ov_ctrl));
		lvhvsw_ov_ctrl |= RT1739_LVSW_OVP_6V;
		lvhvsw_ov_ctrl &= ~RT1739_OT_SEL_LVL;
		RETURN_ERROR(write_reg(port, RT1739_REG_LVHVSW_OV_CTRL,
				       lvhvsw_ov_ctrl));
		RETURN_ERROR(
			read_reg(port, RT1739_REG_VCONN_CTRL4, &vconn_ctrl4));
		vconn_ctrl4 &= ~RT1739_VCONN_OCP_SEL_MASK;
		vconn_ctrl4 |= RT1739_VCONN_OCP_SEL_600MA;
		RETURN_ERROR(
			write_reg(port, RT1739_REG_VCONN_CTRL4, vconn_ctrl4));
		RETURN_ERROR(update_reg(port, RT1739_REG_VCONN_CTRL3,
					RT1739_VCONN_CLIMIT_EN, MASK_CLR));
		break;

	default:
		CPRINTF("RT1739 unknown device id: %02X", device_id);
		break;
	}

	return EC_SUCCESS;
}

static int rt1739_set_frs_enable(int port, int enable)
{
	/* Enable FRS RX detect */
	RETURN_ERROR(update_reg(port, RT1739_REG_CC_FRS_CTRL1, RT1739_FRS_RX_EN,
				enable ? MASK_SET : MASK_CLR));
	/* b/296988176: disable SRCP and OSCS mask while FRS enabled */
	RETURN_ERROR(update_reg(port, RT1739_REG_VBUS_DEG_TIME,
				RT1739_FRS_SRCP_MASK | RT1739_FRS_OSCS_MASK,
				enable ? MASK_SET : MASK_CLR));

	/*
	 * To enable FRS, turn on FRS_RX interrupt and disable
	 * all other interrupts (currently bc1.2 only).
	 *
	 * When interrupt triggered, we can always assume it's a FRS
	 * event without spending extra time to read the flags.
	 */
	RETURN_ERROR(update_reg(port, RT1739_REG_INT_MASK5,
				RT1739_BC12_SNK_DONE_MASK,
				enable ? MASK_CLR : MASK_SET));
	RETURN_ERROR(update_reg(port, RT1739_REG_INT_MASK4, RT1739_FRS_RX_MASK,
				enable ? MASK_SET : MASK_CLR));
	if (enable)
		atomic_or(&flags[port], RT1739_FLAGS_FRS_ENABLED);
	else
		atomic_clear_bits(&flags[port], RT1739_FLAGS_FRS_ENABLED);

	/* Clear Rx receive events. */
	atomic_clear_bits(&flags[port], RT1739_FLAGS_FRS_RX_RECV);

	return EC_SUCCESS;
}

static int rt1739_src_oc(enum tcpc_rp_value rp)
{
	switch (rp) {
	case TYPEC_RP_3A0:
		return RT1739_LV_SRC_OCP_SEL_3_3A;
	case TYPEC_RP_1A5:
		return RT1739_LV_SRC_OCP_SEL_1_75A;
	default:
		return RT1739_LV_SRC_OCP_SEL_1_25A;
	}
}

static int rt1739_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int reg;

	RETURN_ERROR(read_reg(port, RT1739_REG_VBUS_OC_SETTING, &reg));
	reg = (reg & ~RT1739_LV_SRC_OCP_MASK) | rt1739_src_oc(rp);
	return write_reg(port, RT1739_REG_VBUS_OC_SETTING, reg);
}

int rt1739_init(int port)
{
	int device_id, oc_setting, sys_ctrl, vbus_switch_ctrl;
	bool batt_connected = false;

	atomic_clear(&flags[port]);

	RETURN_ERROR(read_reg(port, RT1739_REG_SYS_CTRL, &sys_ctrl));
	RETURN_ERROR(
		read_reg(port, RT1739_REG_VBUS_SWITCH_CTRL, &vbus_switch_ctrl));

	if (IS_ENABLED(CONFIG_BATTERY_FUEL_GAUGE)) {
		batt_connected = (battery_get_disconnect_state() ==
				  BATTERY_NOT_DISCONNECTED);
	}

	if (sys_ctrl & RT1739_DEAD_BATTERY) {
		/*
		 * Dead battery boot, see b/267412033#comment6 for the init
		 * sequence.
		 */
		RETURN_ERROR(
			write_reg(port, RT1739_REG_SYS_CTRL,
				  RT1739_DEAD_BATTERY | RT1739_SHUTDOWN_OFF));
		rt1739_vbus_sink_enable(port, true);
		RETURN_ERROR(write_reg(port, RT1739_REG_SYS_CTRL,
				       RT1739_OT_EN | RT1739_SHUTDOWN_OFF));
	} else if (batt_connected || !(vbus_switch_ctrl & RT1739_HV_SNK_EN)) {
		/* b/275294155: reset vbus switch only instead of doing a full
		 * reset
		 */
		RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_SWITCH_CTRL, 0));
	}
	RETURN_ERROR(write_reg(port, RT1739_REG_SYS_CTRL,
			       RT1739_OT_EN | RT1739_SHUTDOWN_OFF));

	RETURN_ERROR(rt1739_workaround(port));
	RETURN_ERROR(rt1739_set_frs_enable(port, false));
	RETURN_ERROR(rt1739_set_vconn(port, false));
	RETURN_ERROR(update_reg(port, RT1739_REG_VBUS_DET_EN,
				RT1739_VBUS_PRESENT_EN, MASK_SET));
	RETURN_ERROR(update_reg(port, RT1739_REG_SBU_CTRL_01,
				RT1739_DM_SWEN | RT1739_DP_SWEN, MASK_SET));
	RETURN_ERROR(update_reg(port, RT1739_REG_SBU_CTRL_01,
				RT1739_SBUSW_MUX_SEL, MASK_CLR));

	/* VBUS OVP -> 23V */
	RETURN_ERROR(write_reg(
		port, RT1739_REG_VBUS_OV_SETTING,
		(RT1739_OVP_SEL_23_0V << RT1739_VBUS_OVP_SEL_SHIFT) |
			(RT1739_OVP_SEL_23_0V << RT1739_VIN_HV_OVP_SEL_SHIFT)));
	/* VBUS OCP -> 3.3A (or 5.5A for ES2 HV Sink) */
	RETURN_ERROR(rt1739_get_device_id(port, &device_id));
	oc_setting = RT1739_OCP_TIMEOUT_SEL_16MS;
	if (device_id == RT1739_DEVICE_ID_ES2)
		oc_setting |= RT1739_HV_SINK_OCP_SEL_5_5A;
	else
		oc_setting |= RT1739_HV_SINK_OCP_SEL_3_3A;
#if defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	oc_setting |= rt1739_src_oc(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT);
#else
	oc_setting |= rt1739_src_oc(CONFIG_USB_PD_PULLUP);
#endif
	RETURN_ERROR(write_reg(port, RT1739_REG_VBUS_OC_SETTING, oc_setting));

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_CHARGER
static int rt1739_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static void rt1739_update_charge_manager(int port,
					 enum charge_supplier new_bc12_type)
{
	static enum charge_supplier current_bc12_type = CHARGE_SUPPLIER_NONE;

	if (new_bc12_type != current_bc12_type) {
		if (current_bc12_type != CHARGE_SUPPLIER_NONE)
			charge_manager_update_charge(current_bc12_type, port,
						     NULL);

		if (new_bc12_type != CHARGE_SUPPLIER_NONE) {
			struct charge_port_info chg = {
				.current = rt1739_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, port, &chg);
		}

		current_bc12_type = new_bc12_type;
	}
}

static void rt1739_enable_bc12_detection(int port, bool enable)
{
	update_reg(port, RT1739_REG_BC12_SNK_FUNC, RT1739_BC12_SNK_EN,
		   enable ? MASK_SET : MASK_CLR);
}

static enum charge_supplier rt1739_bc12_get_device_type(int port)
{
	int reg, bc12_type;

	if (read_reg(port, RT1739_REG_BC12_STAT, &reg))
		return CHARGE_SUPPLIER_NONE;

	bc12_type = reg & RT1739_PORT_STAT_MASK;
	switch (bc12_type) {
	case RT1739_PORT_STAT_SDP:
		CPRINTS("BC12 SDP");
		return CHARGE_SUPPLIER_BC12_SDP;
	case RT1739_PORT_STAT_CDP:
		CPRINTS("BC12 CDP");
		return CHARGE_SUPPLIER_BC12_CDP;
	case RT1739_PORT_STAT_DCP:
		CPRINTS("BC12 DCP");
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		CPRINTS("BC12 UNKNOWN 0x%02X", bc12_type);
		return CHARGE_SUPPLIER_NONE;
	}
}

static void rt1739_usb_charger_task_init(const int port)
{
	rt1739_enable_bc12_detection(port, false);
}

static void rt1739_usb_charger_task_event(const int port, uint32_t evt)
{
	bool is_non_pd_sink = !pd_capable(port) &&
			      !usb_charger_port_is_sourcing_vbus(port) &&
			      pd_check_vbus_level(port, VBUS_PRESENT);

	/* vbus change, start bc12 detection */
	if (evt & USB_CHG_EVENT_VBUS) {
		if (is_non_pd_sink) {
			if (!(rt1739_pd_connect_flag & BIT(port))) {
				update_reg(port, RT1739_REG_SBU_CTRL_01,
					   RT1739_DM_SWEN | RT1739_DP_SWEN |
						   RT1739_SBU1_SWEN |
						   RT1739_SBU2_SWEN,
					   MASK_SET);
			}
			rt1739_enable_bc12_detection(port, true);
		} else
			rt1739_update_charge_manager(port,
						     CHARGE_SUPPLIER_NONE);
	}

	/* detection done, update charge_manager and stop detection */
	if (evt & USB_CHG_EVENT_BC12) {
		enum charge_supplier supplier;

		if (is_non_pd_sink)
			supplier = rt1739_bc12_get_device_type(port);
		else
			supplier = CHARGE_SUPPLIER_NONE;
		rt1739_update_charge_manager(port, supplier);
		rt1739_enable_bc12_detection(port, false);
	}
}
#endif /* CONFIG_USB_CHARGER */

static atomic_t pending_events;

void rt1739_deferred_interrupt(void)
{
	atomic_t current = atomic_clear(&pending_events);

	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port) {
		int event4, event5;

		if (!(current & BIT(port)))
			continue;

		if (ppc_chips[port].drv != &rt1739_ppc_drv)
			continue;

		if (read_reg(port, RT1739_REG_INT_EVENT4, &event4))
			continue;
		if (read_reg(port, RT1739_REG_INT_EVENT5, &event5))
			continue;

#ifdef CONFIG_USB_CHARGER
		if (event5 & RT1739_BC12_SNK_DONE_INT)
			usb_charger_task_set_event(port, USB_CHG_EVENT_BC12);
#endif /* CONFIG_USB_CHARGER */

		/* write to clear EVENT4 since FRS interrupt has been handled */
		write_reg(port, RT1739_REG_INT_EVENT4, event4);
		write_reg(port, RT1739_REG_INT_EVENT5, event5);
	}
}
DECLARE_DEFERRED(rt1739_deferred_interrupt);

void rt1739_interrupt(int port)
{
	/* The RX event maybe sent out multiple times during one FRS RX
	 * event. Filter the redundant ones.
	 */
	if (flags[port] & RT1739_FLAGS_FRS_ENABLED &&
	    !(flags[port] & RT1739_FLAGS_FRS_RX_RECV)) {
		atomic_or(&flags[port], RT1739_FLAGS_FRS_RX_RECV);
		pd_got_frs_signal(port);
	}

	atomic_or(&pending_events, BIT(port));
	hook_call_deferred(&rt1739_deferred_interrupt_data, 0);
}

/* disconnect SBU, DP, DM when unused to save power */
void rt1739_pd_connect(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; ++i) {
		if (ppc_chips[i].drv == &rt1739_ppc_drv && pd_is_connected(i)) {
			update_reg(i, RT1739_REG_SBU_CTRL_01,
				   RT1739_DM_SWEN | RT1739_DP_SWEN |
					   RT1739_SBU1_SWEN | RT1739_SBU2_SWEN,
				   MASK_SET);
			rt1739_pd_connect_flag |= BIT(i);
		}
	}
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, rt1739_pd_connect, HOOK_PRIO_DEFAULT);

void rt1739_pd_disconnect(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; ++i) {
		if (ppc_chips[i].drv == &rt1739_ppc_drv &&
		    pd_is_disconnected(i)) {
			update_reg(i, RT1739_REG_SBU_CTRL_01,
				   RT1739_DM_SWEN | RT1739_DP_SWEN |
					   RT1739_SBU1_SWEN | RT1739_SBU2_SWEN,
				   MASK_CLR);
			rt1739_pd_connect_flag &= ~BIT(i);
		}
	}
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, rt1739_pd_disconnect, HOOK_PRIO_DEFAULT);

const struct ppc_drv rt1739_ppc_drv = {
	.init = &rt1739_init,
	.is_sourcing_vbus = &rt1739_is_sourcing_vbus,
	.vbus_sink_enable = &rt1739_vbus_sink_enable,
	.vbus_source_enable = &rt1739_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &rt1739_dump,
#endif
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &rt1739_is_vbus_present,
#endif
	.set_vbus_source_current_limit = &rt1739_set_vbus_source_current_limit,
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &rt1739_set_polarity,
#endif
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &rt1739_set_vconn,
#endif
#ifdef CONFIG_USB_PD_FRS_PPC
	.set_frs_enable = &rt1739_set_frs_enable,
#endif
	.interrupt = &rt1739_interrupt,
};

#ifdef CONFIG_USB_CHARGER
const struct bc12_drv rt1739_bc12_drv = {
	.usb_charger_task_init = rt1739_usb_charger_task_init,
	.usb_charger_task_event = rt1739_usb_charger_task_event,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &rt1739_bc12_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
#endif /* CONFIG_USB_CHARGER */
