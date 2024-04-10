/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas RAA489000 TCPC driver
 */

#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "i2c.h"
#include "raa489000.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"

#define DEFAULT_R_AC 20
#define R_AC CONFIG_CHARGER_SENSE_RESISTOR_AC
#define AC_CURRENT_TO_REG(CUR) ((CUR) * R_AC / DEFAULT_R_AC)

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static int dev_id[CONFIG_USB_PD_PORT_MAX_COUNT] = { -1 };

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int raa489000_enter_low_power_mode(int port)
{
	int rv;

	rv = tcpc_write16(port, RAA489000_PD_PHYSICAL_SETTING1, 0);
	if (rv)
		CPRINTS("RAA489000(%d): Failed to set PD PHY setting1!", port);

	rv = tcpc_write16(port, RAA489000_TCPC_SETTING1, 0);
	if (rv)
		CPRINTS("RAA489000(%d): Failed to set TCPC setting1!", port);

	return tcpci_enter_low_power_mode(port);
}
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

/* Configure output current in the TCPC because it is controlling Vbus */
int raa489000_set_output_current(int port, enum tcpc_rp_value rp)
{
	int regval;
	int selected_cur = rp == TYPEC_RP_3A0 ?
				   RAA489000_VBUS_CURRENT_TARGET_3A :
				   RAA489000_VBUS_CURRENT_TARGET_1_5A;

	regval = AC_CURRENT_TO_REG(selected_cur) +
		 selected_cur % (DEFAULT_R_AC / R_AC);

	return tcpc_write16(port, RAA489000_VBUS_CURRENT_TARGET, regval);
}

int raa489000_init(int port)
{
	int rv;
	int regval;
	int device_id;
	int i2c_port;
	struct charge_port_info chg = { 0 };
	int vbus_mv = 0;

	/* Perform unlock sequence */
	rv = tcpc_write16(port, 0xAA, 0xDAA0);
	if (rv)
		CPRINTS("c%d: failed unlock step1", port);
	rv = tcpc_write16(port, 0xAA, 0xACE0);
	if (rv)
		CPRINTS("c%d: failed unlock step2", port);
	rv = tcpc_write16(port, 0xAA, 0x0D0B);
	if (rv)
		CPRINTS("c%d: failed unlock step3", port);

	device_id = -1;
	rv = tcpc_read16(port, TCPC_REG_BCD_DEV, &device_id);
	if (rv)
		CPRINTS("C%d: Failed to read DEV_ID", port);
	CPRINTS("%s(%d): DEVICE_ID=%d", __func__, port, device_id);
	dev_id[port] = device_id;

	/* Enable the ADC */
	/*
	 * TODO(b:147316511) Since this register can be accessed by multiple
	 * tasks, we should add a mutex when modifying this register.
	 *
	 * See(b:178981107,b:178728138) When the battery does not exist,
	 * we must enable ADC function so that charger_get_vbus_voltage
	 * can get the correct voltage.
	 */
	i2c_port = tcpc_config[port].i2c_info.port;
	rv = i2c_read16(i2c_port, ISL923X_ADDR_FLAGS, ISL9238_REG_CONTROL3,
			&regval);
	regval |= RAA489000_ENABLE_ADC;
	rv |= i2c_write16(i2c_port, ISL923X_ADDR_FLAGS, ISL9238_REG_CONTROL3,
			  regval);
	if (rv)
		CPRINTS("c%d: failed to enable ADCs", port);

	/* Enable Vbus detection */
	rv = tcpc_write(port, TCPC_REG_COMMAND,
			TCPC_REG_COMMAND_ENABLE_VBUS_DETECT);
	if (rv)
		CPRINTS("c%d: failed to enable vbus detect cmd", port);

	/*
	 * If VBUS is present, start sinking from it if we haven't already
	 * chosen a charge port and no battery is connected.  This is
	 * *kinda hacky* doing it here, but we must start sinking VBUS now,
	 * otherwise the board may die (See b/150702984, b/178728138).  This
	 * works as this part is a combined charger IC and TCPC.
	 */
	crec_usleep(853);
	charger_get_vbus_voltage(port, &vbus_mv);

	/*
	 * Disable the ADC
	 *
	 * See(b:178356507)  9mW is reduced on S0iX power consumption
	 * by clearing 'Enable ADC' bit.
	 */
	if (IS_ENABLED(CONFIG_OCPC) && (port != 0)) {
		i2c_port = tcpc_config[port].i2c_info.port;
		rv = i2c_read16(i2c_port, ISL923X_ADDR_FLAGS,
				ISL9238_REG_CONTROL3, &regval);
		regval &= ~RAA489000_ENABLE_ADC;
		rv |= i2c_write16(i2c_port, ISL923X_ADDR_FLAGS,
				  ISL9238_REG_CONTROL3, regval);
		if (rv)
			CPRINTS("c%d: failed to disable ADCs", port);
	}

	if ((vbus_mv > 3900) &&
	    charge_manager_get_active_charge_port() == CHARGE_PORT_NONE &&
	    !pd_is_battery_capable()) {
		chg.current = 500;
		chg.voltage = 5000;
		charge_manager_update_charge(CHARGE_SUPPLIER_TYPEC, port, &chg);
		board_set_active_charge_port(port);
	}

	if (device_id > 1) {
		/*
		 * A1 silicon has a DEVICE_ID of 1.  For B0 and newer, we need
		 * allow the TCPC to control VBUS in order to start VBUS ADC
		 * sampling.  This is a requirement to clear the TCPC
		 * initialization status but in POWER_STATUS.  Otherwise, the
		 * common TCPCI init will fail. (See b/154191301)
		 */
		rv = tcpc_read16(port, RAA489000_TCPC_SETTING1, &regval);
		regval |= RAA489000_TCPC_PWR_CNTRL;
		rv = tcpc_write16(port, RAA489000_TCPC_SETTING1, regval);
		if (rv)
			CPRINTS("C%d: failed to set TCPC power control", port);
	}

	/* Note: registers may not be ready until TCPCI init succeeds */
	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

	/*
	 * Set some vendor defined registers to enable the CC comparators and
	 * remove the dead battery resistors.  This only needs to be done on
	 * early silicon versions.
	 */
	if (device_id <= 1) {
		rv = tcpc_write16(port, RAA489000_TYPEC_SETTING1,
				  RAA489000_SETTING1_RDOE |
					  RAA489000_SETTING1_CC2_CMP3_EN |
					  RAA489000_SETTING1_CC2_CMP2_EN |
					  RAA489000_SETTING1_CC2_CMP1_EN |
					  RAA489000_SETTING1_CC1_CMP3_EN |
					  RAA489000_SETTING1_CC1_CMP2_EN |
					  RAA489000_SETTING1_CC1_CMP1_EN |
					  RAA489000_SETTING1_CC_DB_EN);
		if (rv)
			CPRINTS("c%d: failed to enable CC comparators", port);
	}

	/* Set Rx enable for receiver comparator */
	rv = tcpc_read16(port, RAA489000_PD_PHYSICAL_SETTING1, &regval);
	regval |= RAA489000_PD_PHY_SETTING1_RECEIVER_EN |
		  RAA489000_PD_PHY_SETTING1_SQUELCH_EN |
		  RAA489000_PD_PHY_SETTING1_TX_LDO11_EN;
	rv |= tcpc_write16(port, RAA489000_PD_PHYSICAL_SETTING1, regval);
	if (rv)
		CPRINTS("c%d: failed to set PD PHY setting1", port);

	/*
	 * Disable VBUS auto discharge, we'll turn it on later as its needed to
	 * goodcrc.
	 */
	rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &regval);
	regval &= ~TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT;
	rv |= tcpc_write(port, TCPC_REG_POWER_CTRL, regval);
	if (rv)
		CPRINTS("c%d: failed to set auto discharge", port);

	if (device_id <= 1) {
		/* The vendor says to set this setting. */
		rv = tcpc_write16(port, RAA489000_PD_PHYSICAL_PARAMETER1,
				  0x6C07);
		if (rv)
			CPRINTS("c%d: failed to set PD PHY PARAM1", port);
	}

	/* Enable the correct TCPCI interface version */
	rv = tcpc_read16(port, RAA489000_TCPC_SETTING1, &regval);
	if (!(tcpc_config[port].flags & TCPC_FLAGS_TCPCI_REV2_0))
		regval |= RAA489000_TCPCV1_0_EN;
	else
		regval &= ~RAA489000_TCPCV1_0_EN;

	if (device_id <= 1) {
		/* Allow the TCPC to control VBUS. */
		regval |= RAA489000_TCPC_PWR_CNTRL;
	}

	rv = tcpc_write16(port, RAA489000_TCPC_SETTING1, regval);
	if (rv)
		CPRINTS("c%d: failed to set TCPCIv1.0 mode", port);

	/*
	 * Set Vbus OCP UV here, PD tasks will set target current
	 */
	rv = tcpc_write16(port, RAA489000_VBUS_OCP_UV_THRESHOLD,
			  RAA489000_OCP_THRESHOLD_VALUE);
	if (rv)
		CPRINTS("c%d: failed to set OCP threshold", port);

	/* Set Vbus Target Voltage */
	rv = tcpc_write16(port, RAA489000_VBUS_VOLTAGE_TARGET,
			  RAA489000_VBUS_VOLTAGE_TARGET_5160MV);
	if (rv)
		CPRINTS("c%d: failed to set Vbus Target Voltage", port);

	return rv;
}

int raa489000_tcpm_set_cc(int port, int pull)
{
	int rv;

	rv = tcpci_tcpm_set_cc(port, pull);
	if (dev_id[port] > 1 || rv)
		return rv;

	/* Older silicon needs the TCPM to set RDOE to 1 after setting Rp */
	if (pull == TYPEC_CC_RP)
		rv = tcpc_update16(port, RAA489000_TYPEC_SETTING1,
				   RAA489000_SETTING1_RDOE, MASK_SET);

	return rv;
}

#ifdef CONFIG_CMD_TCPC_DUMP

static const struct tcpc_reg_dump_map raa489000_regs[] = {
	{
		.addr = RAA489000_TCPC_SETTING1,
		.name = "TCPC_SETTING1",
		.size = 2,
	},
	{
		.addr = RAA489000_VBUS_VOLTAGE_TARGET,
		.name = "VBUS_VOLTAGE_TARGET",
		.size = 2,
	},
	{
		.addr = RAA489000_VBUS_CURRENT_TARGET,
		.name = "VBUS_CURRENT_TARGET",
		.size = 2,
	},
	{
		.addr = RAA489000_VBUS_OCP_UV_THRESHOLD,
		.name = "VBUS_OCP_UV_THRESHOLD",
		.size = 2,
	},
	{
		.addr = RAA489000_TYPEC_SETTING1,
		.name = "TYPEC_SETTING1",
		.size = 2,
	},
	{
		.addr = RAA489000_PD_PHYSICAL_SETTING1,
		.name = "PD_PHYSICAL_SETTING1",
		.size = 2,
	},
	{
		.addr = RAA489000_PD_PHYSICAL_PARAMETER1,
		.name = "PD_PHYSICAL_PARAMETER1",
		.size = 2,
	},
};

void raa489000_dump_registers(int port)
{
	tcpc_dump_std_registers(port);
	tcpc_dump_registers(port, raa489000_regs, ARRAY_SIZE(raa489000_regs));
}

#endif

int raa489000_debug_detach(int port)
{
	int rv;
	int power_status;

	/*
	 * Force RAA489000 to see debug detach by running:
	 *
	 * 1. Set POWER_CONTROL. AutoDischargeDisconnect=1
	 * 2. Set ROLE_CONTROL=0x0F(OPEN,OPEN)
	 * 3. Set POWER_CONTROL. AutoDischargeDisconnect=0
	 *
	 * Only if we have sufficient battery or are not sinking.  Otherwise,
	 * we would risk brown-out during the CC open set.
	 */
	RETURN_ERROR(tcpc_read(port, TCPC_REG_POWER_STATUS, &power_status));

	if (!pd_is_battery_capable() &&
	    (power_status & TCPC_REG_POWER_STATUS_SINKING_VBUS))
		return EC_SUCCESS;

	tcpci_tcpc_enable_auto_discharge_disconnect(port, 1);

	rv = tcpci_tcpm_set_cc(port, TYPEC_CC_OPEN);

	tcpci_tcpc_enable_auto_discharge_disconnect(port, 0);

	return rv;
}

/* RAA489000 is a TCPCI compatible port controller */
const struct tcpm_drv raa489000_tcpm_drv = {
	.init = &raa489000_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
#ifdef CONFIG_USB_PD_VBUS_MEASURE_TCPC
	.get_vbus_voltage = &tcpci_get_vbus_voltage,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &raa489000_tcpm_set_cc,
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
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &raa489000_enter_low_power_mode,
	.wake_low_power_mode = &tcpci_wake_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
	.tcpc_enable_auto_discharge_disconnect =
		&tcpci_tcpc_enable_auto_discharge_disconnect,
	.debug_detach = &raa489000_debug_detach,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers = &raa489000_dump_registers,
#endif
#ifdef CONFIG_USB_PD_FRS
	.set_frs_enable = &tcpci_tcpc_fast_role_swap_enable,
#endif
};
