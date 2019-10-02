/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ANX7447 port manager */

#include "common.h"
#include "anx7447.h"
#include "console.h"
#include "hooks.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define ANX7447_VENDOR_ALERT    BIT(15)

#define ANX7447_REG_STATUS      0x82
#define ANX7447_REG_STATUS_LINK BIT(0)

#define ANX7447_REG_HPD         0x83
#define ANX7447_REG_HPD_HIGH    BIT(0)
#define ANX7447_REG_HPD_IRQ     BIT(1)
#define ANX7447_REG_HPD_ENABLE  BIT(2)

#define vsafe5v_min (3800/25)
#define vsafe0v_max (800/25)
/*
 * These interface are workable while ADC is enabled, before
 * calling them should make sure ec driver finished chip initilization.
 */
#define is_equal_greater_safe5v(port) \
		(((anx7447_get_vbus_voltage(port))) > vsafe5v_min)
#define is_equal_greater_safe0v(port) \
		(((anx7447_get_vbus_voltage(port))) > vsafe0v_max)

struct anx_state {
	uint16_t i2c_slave_addr_flags;
};

struct anx_usb_mux {
	int state;
};

static int anx7447_mux_set(int port, mux_state_t mux_state);

static struct anx_state anx[CONFIG_USB_PD_PORT_MAX_COUNT];
static struct anx_usb_mux mux[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * ANX7447 has two co-existence I2C slave addresses, TCPC slave address and
 * SPI slave address. The registers of TCPC slave address are partly compliant
 * with standard USB TCPC specification, and the registers in SPI slave
 * address controls the other functions (ex, hpd_level, mux_switch, and
 * so on). It can't use tcpc_read() and tcpc_write() to access SPI slave
 * address because its slave address has been set as TCPC in the structure
 * tcpc_config_t.
 * anx7447_reg_write() and anx7447_reg_read() are implemented here to access
 * ANX7447 SPI slave address.
 */
const struct anx7447_i2c_addr anx7447_i2c_addrs_flags[] = {
	{AN7447_TCPC0_I2C_ADDR_FLAGS, AN7447_SPI0_I2C_ADDR_FLAGS},
	{AN7447_TCPC1_I2C_ADDR_FLAGS, AN7447_SPI1_I2C_ADDR_FLAGS},
	{AN7447_TCPC2_I2C_ADDR_FLAGS, AN7447_SPI2_I2C_ADDR_FLAGS},
	{AN7447_TCPC3_I2C_ADDR_FLAGS, AN7447_SPI3_I2C_ADDR_FLAGS}
};

static inline int anx7447_reg_write(int port, int reg, int val)
{
	int rv = i2c_write8(tcpc_config[port].i2c_info.port,
			    anx[port].i2c_slave_addr_flags,
			    reg, val);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	pd_device_accessed(port);
#endif
	return rv;
}

static inline int anx7447_reg_read(int port, int reg, int *val)
{
	int rv = i2c_read8(tcpc_config[port].i2c_info.port,
			   anx[port].i2c_slave_addr_flags,
			   reg, val);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	pd_device_accessed(port);
#endif
	return rv;
}

void anx7447_hpd_mode_en(int port)
{
	int reg, rv;

	rv = anx7447_reg_read(port, ANX7447_REG_HPD_CTRL_0, &reg);
	if (rv)
		return;

	reg |= ANX7447_REG_HPD_MODE;
	anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
}

void anx7447_hpd_output_en(int port)
{
	int reg, rv;

	rv = anx7447_reg_read(port, ANX7447_REG_HPD_DEGLITCH_H, &reg);
	if (rv)
		return;

	reg |= ANX7447_REG_HPD_OEN;
	anx7447_reg_write(port, ANX7447_REG_HPD_DEGLITCH_H, reg);
}

void anx7447_set_hpd_level(int port, int hpd_lvl)
{
	int reg, rv;

	rv = anx7447_reg_read(port, ANX7447_REG_HPD_CTRL_0, &reg);
	if (rv)
		return;

	if (hpd_lvl)
		reg |= ANX7447_REG_HPD_OUT;
	else
		reg &= ~ANX7447_REG_HPD_OUT;
	anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
}

#ifdef CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND
static inline void anx7447_reg_write_and(int port, int reg, int v_and)
{
	int val;

	if (!anx7447_reg_read(port, reg, &val))
		anx7447_reg_write(port, reg, (val & v_and));
}

static inline void anx7447_reg_write_or(int port, int reg, int v_or)
{
	int val;

	if (!anx7447_reg_read(port, reg, &val))
		anx7447_reg_write(port, reg, (val | v_or));
}

#define ANX7447_FLASH_DONE_TIMEOUT_US	(100 * MSEC)

static int anx7447_wait_for_flash_done(int port)
{
	timestamp_t deadline;
	int rv;
	int r;

	deadline.val = get_time().val + ANX7447_FLASH_DONE_TIMEOUT_US;
	do {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		rv = anx7447_reg_read(port, ANX7447_REG_R_RAM_CTRL, &r);
		if (rv)
			return rv;
	} while (!(r & ANX7447_R_RAM_CTRL_FLASH_DONE));

	return EC_SUCCESS;
}

static int anx7447_flash_write_en(int port)
{
	anx7447_reg_write(port, ANX7447_REG_FLASH_INST_TYPE,
			  ANX7447_FLASH_INST_TYPE_WRITEENABLE);
	anx7447_reg_write_or(port, ANX7447_REG_R_FLASH_RW_CTRL,
			     ANX7447_R_FLASH_RW_CTRL_GENERAL_INST_EN);
	return anx7447_wait_for_flash_done(port);
}

static int anx7447_flash_op_init(int port)
{
	int rv;

	anx7447_reg_write_or(port, ANX7447_REG_OCM_CTRL_0,
			     ANX7447_OCM_CTRL_OCM_RESET);
	anx7447_reg_write_or(port, ANX7447_REG_ADDR_GPIO_CTRL_0,
			     ANX7447_ADDR_GPIO_CTRL_0_SPI_WP);

	rv = anx7447_flash_write_en(port);
	if (rv)
		return rv;

	anx7447_reg_write_and(port, ANX7447_REG_R_FLASH_STATUS_0,
			      ANX7447_FLASH_STATUS_SPI_STATUS_0);
	anx7447_reg_write_or(port, ANX7447_REG_R_FLASH_RW_CTRL,
			     ANX7447_R_FLASH_RW_CTRL_WRITE_STATUS_EN);

	return anx7447_wait_for_flash_done(port);
}

static int anx7447_flash_is_empty(int port)
{
	int r;

	anx7447_reg_read(port, ANX7447_REG_OCM_VERSION, &r);

	return ((r == 0) ? 1 : 0);
}

static int anx7447_flash_erase_internal(int port, int write_console_if_empty)
{
	int rv;
	int r;

	tcpc_read(port, TCPC_REG_COMMAND, &r);
	usleep(ANX7447_DELAY_IN_US);

	if (anx7447_flash_is_empty(port) == 1) {
		if (write_console_if_empty)
			CPRINTS("C%d: Nothing to erase!", port);
		return EC_SUCCESS;
	}
	CPRINTS("C%d: Erasing OCM flash...", port);

	rv = anx7447_flash_op_init(port);
	if (rv)
		return rv;

	usleep(ANX7447_DELAY_IN_US);

	rv = anx7447_flash_write_en(port);
	if (rv)
		return rv;

	anx7447_reg_write(port, ANX7447_REG_FLASH_ERASE_TYPE,
			  ANX7447_FLASH_ERASE_TYPE_CHIPERASE);
	anx7447_reg_write_or(port, ANX7447_REG_R_FLASH_RW_CTRL,
			     ANX7447_R_FLASH_RW_CTRL_FLASH_ERASE_EN);

	return anx7447_wait_for_flash_done(port);
}

int anx7447_flash_erase(int port)
{
	return anx7447_flash_erase_internal(port,
					    0 /* suppress console if empty */);
}

/* Add console command to erase OCM flash if needed. */
static int command_anx_ocm(int argc, char **argv)
{
	char *e = NULL;
	int port;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Get port number from first parameter */
	port = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		int rv;
		if (strcasecmp(argv[2], "erase"))
			return EC_ERROR_PARAM2;
		rv = anx7447_flash_erase_internal(
			port, 1 /* write to console if empty */);
		if (rv)
			ccprintf("C%d: Failed to erase OCM flash (%d)\n",
				 port, rv);
	}

	ccprintf("C%d: OCM flash is %sempty.\n",
		port, anx7447_flash_is_empty(port) ? "" : "not ");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(anx_ocm, command_anx_ocm,
			"port [erase]",
			"Print OCM status or erases OCM for a given port.");
#endif

static int anx7447_init(int port)
{
	int rv, reg, i;

	ASSERT(port < CONFIG_USB_PD_PORT_MAX_COUNT);

	memset(&anx[port], 0, sizeof(struct anx_state));

	/*
	 * find corresponding anx7447 SPI slave address according to
	 * specified TCPC slave address
	 */
	for (i = 0; i < ARRAY_SIZE(anx7447_i2c_addrs_flags); i++) {
		if (I2C_GET_ADDR(tcpc_config[port].i2c_info.addr_flags) ==
		    I2C_GET_ADDR(
			    anx7447_i2c_addrs_flags[i].tcpc_slave_addr_flags)) {
			anx[port].i2c_slave_addr_flags =
				anx7447_i2c_addrs_flags[i].spi_slave_addr_flags;
			break;
		}
	}
	if (!I2C_GET_ADDR(anx[port].i2c_slave_addr_flags)) {
		ccprintf("TCPC I2C slave addr 0x%x is invalid for ANX7447\n",
			 I2C_GET_ADDR(tcpc_config[port]
				      .i2c_info.addr_flags));
		return EC_ERROR_UNKNOWN;
	}


	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND
	/* Check and print OCM status to console. */
	CPRINTS("C%d: OCM flash is %sempty",
		port, anx7447_flash_is_empty(port) ? "" : "not ");
#endif

	/*
	 * 7447 has a physical pin to detect the presence of VBUS, VBUS_SENSE
	 * , and 7447 has a VBUS current protection mechanism through another
	 * pin input VBUS_OCP. To enable VBUS OCP, OVP protection, driver needs
	 * to set the threshold to the registers VBUS_VOLTAGE_ALARM_HI_CFG
	 * (0x76 & 0x77) and VBUS_OCP_HI_THRESHOLD (0xDD &0xDE). These values
	 * could be customized based on different platform design.
	 * Disable VBUS protection here since the default values of
	 * VBUS_VOLTAGE_ALARM_HI_CFG and VBUS_OCP_HI_THRESHOLD are zero.
	 */
	rv = tcpc_read(port, ANX7447_REG_TCPC_CTRL_2, &reg);
	if (rv)
		return rv;
	reg &= ~ANX7447_REG_ENABLE_VBUS_PROTECT;
	rv = tcpc_write(port, ANX7447_REG_TCPC_CTRL_2, reg);
	if (rv)
		return rv;

	/* ADC enable, use to monitor VBUS voltage */
	rv = tcpc_read(port, ANX7447_REG_ADC_CTRL_1, &reg);
	if (rv)
		return rv;
	reg |= ANX7447_REG_ADCFSM_EN;
	rv = tcpc_write(port, ANX7447_REG_ADC_CTRL_1, reg);
	if (rv)
		return rv;

	/* Set VCONN OCP(Over Current Protection) threshold */
	rv = tcpc_read(port, ANX7447_REG_ANALOG_CTRL_8, &reg);
	if (rv)
		return rv;
	reg &= ~ANX7447_REG_VCONN_OCP_MASK;
	reg |= ANX7447_REG_VCONN_OCP_440mA;
	rv = tcpc_write(port, ANX7447_REG_ANALOG_CTRL_8, reg);

	/* Vconn SW protection time of inrush current */
	rv = tcpc_read(port, ANX7447_REG_ANALOG_CTRL_10, &reg);
	if (rv)
		return rv;
	reg &= ~ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_MASK;
	reg |= ANX7447_REG_R_VCONN_PWR_PRT_INRUSH_TIME_2430US;
	rv = tcpc_write(port, ANX7447_REG_ANALOG_CTRL_10, reg);

#ifdef CONFIG_USB_PD_TCPM_MUX
	/*
	 * Run mux_set() here for considering CCD(Case-Closed Debugging) case
	 * If this TCPC is not also the MUX then don't initialize to NONE
	 */
	if (!(usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC))
		rv |= anx7447_mux_set(port, TYPEC_MUX_NONE);
#endif /* CONFIG_USB_PD_TCPM_MUX */

	return rv;
}

static int anx7447_release(int port)
{
	return EC_SUCCESS;
}

static void anx7447_update_hpd_enable(int port)
{
	int status, reg, rv;

	rv = tcpc_read(port, ANX7447_REG_STATUS, &status);
	rv |= tcpc_read(port, ANX7447_REG_HPD, &reg);
	if (rv)
		return;

	if (!(reg & ANX7447_REG_HPD_ENABLE) ||
	    !(status & ANX7447_REG_STATUS_LINK)) {
		reg &= ~ANX7447_REG_HPD_IRQ;
		tcpc_write(port, ANX7447_REG_HPD,
			   (status & ANX7447_REG_STATUS_LINK)
			   ? reg | ANX7447_REG_HPD_ENABLE
			   : reg & ~ANX7447_REG_HPD_ENABLE);
	}
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
static int anx7447_get_vbus_voltage(int port)
{
	int vbus_volt = 0;

	tcpc_read16(port, TCPC_REG_VBUS_VOLTAGE, &vbus_volt);

	return vbus_volt;
}

int anx7447_set_power_supply_ready(int port)
{
	int count = 0;

	while (is_equal_greater_safe0v(port)) {
		if (count >= 10)
			break;
		msleep(100);
		count++;
	}

	return tcpc_write(port, TCPC_REG_COMMAND, 0x77);
}
#endif /* CONFIG_USB_PD_VBUS_DETECT_TCPC */

int anx7447_power_supply_reset(int port)
{
	return tcpc_write(port, TCPC_REG_COMMAND, 0x66);
}

int anx7447_board_charging_enable(int port, int enable)
{
	return tcpc_write(port, TCPC_REG_COMMAND, enable ? 0x55 : 0x44);
}

static void anx7447_tcpc_alert(int port)
{
	int alert, rv;

	rv = tcpc_read16(port, TCPC_REG_ALERT, &alert);
	/* process and clear alert status */
	tcpci_tcpc_alert(port);

	if (!rv && (alert & ANX7447_VENDOR_ALERT))
		anx7447_update_hpd_enable(port);
}

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

void anx7447_tcpc_update_hpd_status(int port, int hpd_lvl, int hpd_irq)
{
	int reg = 0;

	/*
	 * All calls within this method need to update to a mux_read/write calls
	 * that use the secondary address. This is a non-trival change and no
	 * one is using the anx7447 as a mux only (and probably never will since
	 * it doesn't have a re-driver). If that changes, we need to update this
	 * code.
	 */
	ASSERT(!(usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC));

	anx7447_set_hpd_level(port, hpd_lvl);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_deadline[port])
			usleep(hpd_deadline[port] - now);

		anx7447_reg_read(port, ANX7447_REG_HPD_CTRL_0, &reg);
		reg &= ~ANX7447_REG_HPD_OUT;
		anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		reg |= ANX7447_REG_HPD_OUT;
		anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

void anx7447_tcpc_clear_hpd_status(int port)
{
	anx7447_hpd_output_en(port);
	anx7447_set_hpd_level(port, 0);
}

#ifdef CONFIG_USB_PD_TCPM_MUX
static int anx7447_mux_init(int port)
{
	ASSERT(port < CONFIG_USB_PD_PORT_MAX_COUNT);

	memset(&mux[port], 0, sizeof(struct anx_usb_mux));

	/* init hpd status */
	anx7447_hpd_mode_en(port);
	anx7447_set_hpd_level(port, 0);
	anx7447_hpd_output_en(port);

	/*
	 * ANX initializes its muxes to (MUX_USB_ENABLED | MUX_DP_ENABLED)
	 * when reinitialized, we need to force initialize it to
	 * TYPEC_MUX_NONE
	 */
	return anx7447_mux_set(port, TYPEC_MUX_NONE);
}

#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
static void anx7447_mux_safemode(int port, int on_off)
{
	int reg;

	mux_read(port, ANX7447_REG_ANALOG_CTRL_9, &reg);

	if (on_off)
		reg |= ANX7447_REG_SAFE_MODE;
	else
		reg &= ~(ANX7447_REG_SAFE_MODE);

	mux_write(port, ANX7447_REG_ANALOG_CTRL_9, reg);
	CPRINTS("C%d set mux to safemode %s, reg = 0x%x",
		port, (on_off) ? "on" : "off", reg);
}

static inline void anx7447_configure_aux_src(int port, int on_off)
{
	int reg;

	mux_read(port, ANX7447_REG_ANALOG_CTRL_9, &reg);

	if (on_off)
		reg |= ANX7447_REG_R_AUX_RES_PULL_SRC;
	else
		reg &= ~(ANX7447_REG_R_AUX_RES_PULL_SRC);

	mux_write(port, ANX7447_REG_ANALOG_CTRL_9, reg);

	CPRINTS("C%d set aux_src to %s, reg = 0x%x",
		port, (on_off) ? "on" : "off", reg);
}
#endif

/*
 * Set mux.
 *
 * sstx and ssrx are the USB superspeed transmit and receive pairs. ml is the
 * DisplayPort Main Link. There are four lanes total. For example, DP cases
 * connect them all and dock cases connect 2 DP and USB.
 *
 * a2, a3, a10, a11, b2, b3, b10, b11 are pins on the USB-C connector.
 */
static int anx7447_mux_set(int port, mux_state_t mux_state)
{
	int cc_direction;
	mux_state_t mux_type;
	int sw_sel = 0x00, aux_sw = 0x00;
	int rv;

	cc_direction = mux_state & MUX_POLARITY_INVERTED;
	mux_type = mux_state & TYPEC_MUX_DOCK;
	CPRINTS("C%d mux_state = 0x%x, mux_type = 0x%x",
		port, mux_state, mux_type);
	if (cc_direction == 0) {
		/* cc1 connection */
		if (mux_type == TYPEC_MUX_DOCK) {
			/* ml0-a10/11, ml1-b2/b3, sstx-a2/a3, ssrx-b10/11 */
			sw_sel = 0x21;
			/* aux+ <-> sbu1, aux- <-> sbu2 */
			aux_sw = 0x03;
		} else if (mux_type == TYPEC_MUX_DP) {
			/* ml0-a10/11, ml1-b2/b3, ml2-a2/a3, ml3-b10/11 */
			sw_sel = 0x09;
			/* aux+ <-> sbu1, aux- <-> sbu2 */
			aux_sw = 0x03;
		} else if (mux_type == TYPEC_MUX_USB) {
			/* ssrxp<->b11, ssrxn<->b10, sstxp<->a2, sstxn<->a3 */
			sw_sel = 0x20;
		}
	} else {
		/* cc2 connection */
		if (mux_type == TYPEC_MUX_DOCK) {
			/* ml0-b10/11, ml1-a2/b3, sstx-b2/a3, ssrx-a10/11 */
			sw_sel = 0x12;
			/* aux+ <-> sbu2, aux- <-> sbu1 */
			aux_sw = 0x0C;
		} else if (mux_type == TYPEC_MUX_DP) {
			/* ml0-b10/11, ml1-a2/b3, ml2-b2/a3, ml3-a10/11 */
			sw_sel = 0x06;
			/* aux+ <-> sbu2, aux- <-> sbu1 */
			aux_sw = 0x0C;
		} else if (mux_type == TYPEC_MUX_USB) {
			/* ssrxp<->a11, ssrxn<->a10, sstxp<->b2, sstxn<->b3 */
			sw_sel = 0x10;
		}
	}

	/*
	 * Once need to configure the Mux, should set the mux to safe mode
	 * first. After the  mux configured, should set mux to normal mode.
	 */
#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
	anx7447_mux_safemode(port, 1);
#endif
	rv = mux_write(port, ANX7447_REG_TCPC_SWITCH_0, sw_sel);
	rv |= mux_write(port, ANX7447_REG_TCPC_SWITCH_1, sw_sel);
	rv |= mux_write(port, ANX7447_REG_TCPC_AUX_SWITCH, aux_sw);

	mux[port].state = mux_state;

#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
	/*
	 * DP and Dock mode: after configured the Mux, change the Mux to
	 * normal mode, otherwise: keep safe mode.
	 */
	if (mux_type != TYPEC_MUX_NONE) {
		anx7447_configure_aux_src(port, 1);
		anx7447_mux_safemode(port, 0);
	} else
		anx7447_configure_aux_src(port, 0);
#endif

	return rv;
}

/* current mux state */
static int anx7447_mux_get(int port, mux_state_t *mux_state)
{
	*mux_state = mux[port].state;

	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_TCPM_MUX */

/* ANX7447 is a TCPCI compatible port controller */
const struct tcpm_drv anx7447_tcpm_drv = {
	.init			= &anx7447_init,
	.release		= &anx7447_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &anx7447_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
};

#ifdef CONFIG_USB_PD_TCPM_MUX
const struct usb_mux_driver anx7447_usb_mux_driver = {
	.init = anx7447_mux_init,
	.set = anx7447_mux_set,
	.get = anx7447_mux_get,
};
#endif /* CONFIG_USB_PD_TCPM_MUX */

