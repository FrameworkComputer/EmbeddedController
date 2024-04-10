/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ANX7447 port manager */

#include "anx7447.h"
#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define VSAFE5V_MIN 3800
#define VSAFE0V_MAX 800

struct anx_state {
	uint16_t i2c_addr_flags;
};

struct anx_usb_mux {
	int state;
};

static int anx7447_mux_set(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required);

static struct anx_state anx[CONFIG_USB_PD_PORT_MAX_COUNT];
static struct anx_usb_mux mux[CONFIG_USB_PD_PORT_MAX_COUNT];
static bool anx7447_bist_test_mode[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_USB_PD_FRS_TCPC
/* an array to indicate which port is waiting for FRS disablement. */
static bool anx_frs_dis[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif /* CONFIG_USB_PD_FRS_TCPC */

/*
 * ANX7447 has two co-existence I2C addresses, TCPC address and
 * SPI address. The registers of TCPC address are partly compliant
 * with standard USB TCPC specification, and the registers in SPI
 * address controls the other functions (ex, hpd_level, mux_switch, and
 * so on). It can't use tcpc_read() and tcpc_write() to access SPI
 * address because its address has been set as TCPC in the structure
 * tcpc_config_t.
 * anx7447_reg_write() and anx7447_reg_read() are implemented here to access
 * ANX7447 SPI address.
 */
const struct anx7447_i2c_addr anx7447_i2c_addrs_flags[] = {
	{ AN7447_TCPC0_I2C_ADDR_FLAGS, AN7447_SPI0_I2C_ADDR_FLAGS },
	{ AN7447_TCPC1_I2C_ADDR_FLAGS, AN7447_SPI1_I2C_ADDR_FLAGS },
	{ AN7447_TCPC2_I2C_ADDR_FLAGS, AN7447_SPI2_I2C_ADDR_FLAGS },
	{ AN7447_TCPC3_I2C_ADDR_FLAGS, AN7447_SPI3_I2C_ADDR_FLAGS }
};

static inline int anx7447_reg_write(int port, int reg, int val)
{
	int rv = i2c_write8(tcpc_config[port].i2c_info.port,
			    anx[port].i2c_addr_flags, reg, val);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	pd_device_accessed(port);
#endif
	return rv;
}

static inline int anx7447_reg_read(int port, int reg, int *val)
{
	int rv = i2c_read8(tcpc_config[port].i2c_info.port,
			   anx[port].i2c_addr_flags, reg, val);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	pd_device_accessed(port);
#endif
	return rv;
}

void anx7447_hpd_mode_init(int port)
{
	int reg, rv;

	rv = anx7447_reg_read(port, ANX7447_REG_HPD_CTRL_0, &reg);
	if (rv)
		return;

	/*
	 * Set ANX7447_REG_HPD_MODE bit as 0, then the TCPC will generate the
	 * HPD pulse from internal timer (by using ANX7447_REG_HPD_IRQ0)
	 * instead of using the ANX7447_REG_HPD_OUT to set the HPD IRQ signal.
	 */
	reg &= ~(ANX7447_REG_HPD_MODE | ANX7447_REG_HPD_PLUG |
		 ANX7447_REG_HPD_UNPLUG);
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

	/*
	 * When ANX7447_REG_HPD_MODE is 1, use ANX7447_REG_HPD_OUT
	 * to generate HPD event, otherwise use ANX7447_REG_HPD_UNPLUG
	 * and ANX7447_REG_HPD_PLUG.
	 */
	if (hpd_lvl) {
		reg &= ~ANX7447_REG_HPD_UNPLUG;
		reg |= ANX7447_REG_HPD_PLUG;
	} else {
		reg &= ~ANX7447_REG_HPD_PLUG;
		reg |= ANX7447_REG_HPD_UNPLUG;
	}
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

#define ANX7447_FLASH_DONE_TIMEOUT_US (100 * MSEC)

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

	anx7447_reg_read(port, ANX7447_REG_OCM_MAIN_VERSION, &r);

	return ((r == 0) ? 1 : 0);
}

static int anx7447_flash_erase_internal(int port, int write_console_if_empty)
{
	int rv;
	int r;

	tcpc_read(port, TCPC_REG_COMMAND, &r);
	crec_usleep(ANX7447_DELAY_IN_US);

	if (anx7447_flash_is_empty(port) == 1) {
		if (write_console_if_empty)
			CPRINTS("C%d: Nothing to erase!", port);
		return EC_SUCCESS;
	}
	CPRINTS("C%d: Erasing OCM flash...", port);

	rv = anx7447_flash_op_init(port);
	if (rv)
		return rv;

	crec_usleep(ANX7447_DELAY_IN_US);

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
static int command_anx_ocm(int argc, const char **argv)
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
			ccprintf("C%d: Failed to erase OCM flash (%d)\n", port,
				 rv);
	}

	ccprintf("C%d: OCM flash is %sempty.\n", port,
		 anx7447_flash_is_empty(port) ? "" : "not ");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(anx_ocm, command_anx_ocm, "port [erase]",
			"Print OCM status or erases OCM for a given port.");
#endif

static int anx7447_init(int port)
{
	int rv, reg, i;
	const struct usb_mux_chain *me = &usb_muxes[port];
	bool unused;

	ASSERT(port < CONFIG_USB_PD_PORT_MAX_COUNT);

	memset(&anx[port], 0, sizeof(struct anx_state));

	/*
	 * find corresponding anx7447 SPI address according to
	 * specified TCPC address
	 */
	for (i = 0; i < ARRAY_SIZE(anx7447_i2c_addrs_flags); i++) {
		if (I2C_STRIP_FLAGS(tcpc_config[port].i2c_info.addr_flags) ==
		    I2C_STRIP_FLAGS(
			    anx7447_i2c_addrs_flags[i].tcpc_addr_flags)) {
			anx[port].i2c_addr_flags =
				anx7447_i2c_addrs_flags[i].spi_addr_flags;
			break;
		}
	}
	if (!I2C_STRIP_FLAGS(anx[port].i2c_addr_flags)) {
		ccprintf(
			"TCPC I2C addr 0x%x is invalid for ANX7447\n",
			I2C_STRIP_FLAGS(tcpc_config[port].i2c_info.addr_flags));
		return EC_ERROR_UNKNOWN;
	}

	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

#ifdef CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND
	/* Check and print OCM status to console. */
	CPRINTS("C%d: OCM flash is %sempty", port,
		anx7447_flash_is_empty(port) ? "" : "not ");
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

	/* Set VBUS_VOLTAGE_ALARM_HI threshold */
	RETURN_ERROR(
		tcpc_write16(port, TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG, 0x3FF));
	/* Set VCONN_VOLTAGE_ALARM_HI threshold to 6V */
	RETURN_ERROR(tcpc_write16(port, VCONN_VOLTAGE_ALARM_HI_CFG, 0xF0));

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
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC))
		/* Unmask FRSWAP signal detect */
		tcpc_write(port, ANX7447_REG_VD_ALERT_MASK,
			   ANX7447_FRSWAP_SIGNAL_DETECTED);

#ifdef CONFIG_USB_PD_TCPM_MUX
	/*
	 * Run mux_set() here for considering CCD(Case-Closed Debugging) case
	 * If this TCPC is not also the MUX then don't initialize to NONE
	 */
	while ((me != NULL) && (me->mux->driver != &anx7447_usb_mux_driver))
		me = me->next;

	/*
	 * Note that bypassing the usb_mux API is okay for internal driver calls
	 * since the task calling init already holds this port's mux lock.
	 */
	if (me != NULL && !(me->mux->flags & USB_MUX_FLAG_NOT_TCPC))
		rv = anx7447_mux_set(me->mux, USB_PD_MUX_NONE, &unused);
#endif /* CONFIG_USB_PD_TCPM_MUX */

	return rv;
}

static int anx7447_release(int port)
{
	return EC_SUCCESS;
}

static void anx7447_vendor_defined_alert(int port)
{
	int alert;

	tcpc_read(port, ANX7447_REG_VD_ALERT, &alert);

	/* write to clear alerts */
	tcpc_write(port, ANX7447_REG_VD_ALERT, alert);

	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC) &&
	    alert & ANX7447_FRSWAP_SIGNAL_DETECTED)
		pd_got_frs_signal(port);
}

static void anx7447_tcpc_alert(int port)
{
	int alert;

	tcpc_read16(port, TCPC_REG_ALERT, &alert);
	if (alert & TCPC_REG_ALERT_VENDOR_DEF)
		anx7447_vendor_defined_alert(port);

	/* process and clear alert status */
	tcpci_tcpc_alert(port);
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int anx7447_tcpc_enter_low_power_mode(int port)
{
	int rv;

	/*
	 * if anx7447 is in source mode, need to set Rp to default before
	 * entering the low power mode.
	 */
	if (pd_get_dual_role(port) == PD_DRP_FORCE_SOURCE) {
		rv = tcpc_write(
			port, TCPC_REG_ROLE_CTRL,
			TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, TYPEC_RP_USB,
					       TYPEC_CC_RP, TYPEC_CC_RP));
		if (rv) {
			return rv;
		}
	}

	return tcpci_enter_low_power_mode(port);
}
#endif

#ifdef CONFIG_USB_PD_FRS_TCPC
static void anx7447_disable_frs_deferred(void)
{
	int i, val;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (!anx_frs_dis[i])
			continue;

		anx_frs_dis[i] = false;
		anx7447_reg_read(i, ANX7447_REG_ADDR_GPIO_CTRL_1, &val);
		val &= ~ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA;
		anx7447_reg_write(i, ANX7447_REG_ADDR_GPIO_CTRL_1, val);
	}
}
DECLARE_DEFERRED(anx7447_disable_frs_deferred);

static int anx7447_set_frs_enable(int port, int enable)
{
	int val;

	RETURN_ERROR(tcpc_update8(port, ANX7447_REG_FRSWAP_CTRL,
				  ANX7447_FRSWAP_DETECT_ENABLE,
				  enable ? MASK_SET : MASK_CLR));

	if (!enable) {
		/*
		 * b/223087277#comment52: delay to disable FRS output to the
		 * PPC. Some PPCs need the FRS_EN pin to stay asserted until the
		 * VBUS dropped to a threshold under 5V to successfully source.
		 * However, on some hubs with a larger cap, the VBUS might take
		 * more than 10 ms.  This workaround is to delay the FRS_EN
		 * deassertion to PPC for 30 ms, which should be enough for
		 * most cases.
		 */
		anx_frs_dis[port] = true;
		hook_call_deferred(&anx7447_disable_frs_deferred_data,
				   30 * MSEC);
		return EC_SUCCESS;
	}

	RETURN_ERROR(
		anx7447_reg_read(port, ANX7447_REG_ADDR_GPIO_CTRL_1, &val));
	val |= ANX7447_ADDR_GPIO_CTRL_1_FRS_EN_DATA;
	RETURN_ERROR(
		anx7447_reg_write(port, ANX7447_REG_ADDR_GPIO_CTRL_1, val));
	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_FRS_TCPC */

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

void anx7447_tcpc_update_hpd_status(const struct usb_mux *me,
				    mux_state_t mux_state, bool *ack_required)
{
	int reg = 0;
	int port = me->usb_port;
	int hpd_lvl = (mux_state & USB_PD_MUX_HPD_LVL) ? 1 : 0;
	int hpd_irq = (mux_state & USB_PD_MUX_HPD_IRQ) ? 1 : 0;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/*
	 * All calls within this method need to update to a mux_read/write calls
	 * that use the secondary address. This is a non-trival change and no
	 * one is using the anx7447 as a mux only (and probably never will since
	 * it doesn't have a re-driver). If that changes, we need to update this
	 * code.
	 */
	ASSERT(!(me->flags & USB_MUX_FLAG_NOT_TCPC));

	anx7447_set_hpd_level(port, hpd_lvl);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_deadline[port])
			crec_usleep(hpd_deadline[port] - now);

		/*
		 * For generate hardware HPD IRQ, need clear bit
		 * ANX7447_REG_HPD_IRQ0 first, then set it. This bit is not
		 * write clear.
		 */
		anx7447_reg_read(port, ANX7447_REG_HPD_CTRL_0, &reg);
		reg &= ~ANX7447_REG_HPD_IRQ0;
		anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
		reg |= ANX7447_REG_HPD_IRQ0;
		anx7447_reg_write(port, ANX7447_REG_HPD_CTRL_0, reg);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

#ifdef CONFIG_USB_PD_TCPM_MUX
static int anx7447_mux_init(const struct usb_mux *me)
{
	int port = me->usb_port;
	int i;
	bool unused;
	const uint16_t tcpc_i2c_addr =
		I2C_STRIP_FLAGS(tcpc_config[me->usb_port].i2c_info.addr_flags);
	const uint16_t mux_i2c_addr =
		I2C_STRIP_FLAGS(usb_muxes[port].mux->i2c_addr_flags);

	/*
	 * find corresponding anx7447 SPI address according to
	 * specified MUX address from mux and tcpc i2c addr config.
	 */
	for (i = 0; i < ARRAY_SIZE(anx7447_i2c_addrs_flags); i++) {
		uint16_t i2c_addr_key = I2C_STRIP_FLAGS(
			anx7447_i2c_addrs_flags[i].tcpc_addr_flags);

		if (i2c_addr_key == tcpc_i2c_addr ||
		    i2c_addr_key == mux_i2c_addr) {
			anx[port].i2c_addr_flags =
				anx7447_i2c_addrs_flags[i].spi_addr_flags;
			break;
		}
	}
	if (!I2C_STRIP_FLAGS(anx[port].i2c_addr_flags)) {
		ccprintf("TCPC I2C addr 0x%x is invalid for ANX7447\n",
			 I2C_STRIP_FLAGS(usb_muxes[port].mux->i2c_addr_flags));
		return EC_ERROR_UNKNOWN;
	}

	ASSERT(port < CONFIG_USB_PD_PORT_MAX_COUNT);

	memset(&mux[port], 0, sizeof(struct anx_usb_mux));

	/* init hpd status */
	anx7447_hpd_mode_init(port);
	anx7447_set_hpd_level(port, 0);
	anx7447_hpd_output_en(port);

	/*
	 * ANX initializes its muxes to (USB_PD_MUX_USB_ENABLED |
	 * USB_PD_MUX_DP_ENABLED) when reinitialized, we need to force
	 * initialize it to USB_PD_MUX_NONE
	 */
	return anx7447_mux_set(me, USB_PD_MUX_NONE, &unused);
}

#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
static void anx7447_mux_safemode(const struct usb_mux *me, int on_off)
{
	int reg;

	mux_read(me, ANX7447_REG_ANALOG_CTRL_9, &reg);

	if (on_off)
		reg |= ANX7447_REG_SAFE_MODE;
	else
		reg &= ~(ANX7447_REG_SAFE_MODE);

	mux_write(me, ANX7447_REG_ANALOG_CTRL_9, reg);
	CPRINTS("C%d set mux to safemode %s, reg = 0x%x", me->usb_port,
		(on_off) ? "on" : "off", reg);
}

static inline void anx7447_configure_aux_src(const struct usb_mux *me,
					     int on_off)
{
	int reg;

	mux_read(me, ANX7447_REG_ANALOG_CTRL_9, &reg);

	if (on_off)
		reg |= ANX7447_REG_R_AUX_RES_PULL_SRC;
	else
		reg &= ~(ANX7447_REG_R_AUX_RES_PULL_SRC);

	mux_write(me, ANX7447_REG_ANALOG_CTRL_9, reg);

	CPRINTS("C%d set aux_src to %s, reg = 0x%x", me->usb_port,
		(on_off) ? "on" : "off", reg);
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
static int anx7447_mux_set(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	int cc_direction;
	mux_state_t mux_type;
	int sw_sel = 0x00, aux_sw = 0x00;
	int rv;
	int port = me->usb_port;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	cc_direction = mux_state & USB_PD_MUX_POLARITY_INVERTED;
	mux_type = mux_state & USB_PD_MUX_DOCK;
	CPRINTS("C%d mux_state = 0x%x, mux_type = 0x%x", port, mux_state,
		mux_type);
	if (cc_direction == 0) {
		/* cc1 connection */
		if (mux_type == USB_PD_MUX_DOCK) {
			/* ml0-a10/11, ml1-b2/b3, sstx-a2/a3, ssrx-b10/11 */
			sw_sel = 0x21;
			/* aux+ <-> sbu1, aux- <-> sbu2 */
			aux_sw = 0x03;
		} else if (mux_type == USB_PD_MUX_DP_ENABLED) {
			/* ml0-a10/11, ml1-b2/b3, ml2-a2/a3, ml3-b10/11 */
			sw_sel = 0x09;
			/* aux+ <-> sbu1, aux- <-> sbu2 */
			aux_sw = 0x03;
		} else if (mux_type == USB_PD_MUX_USB_ENABLED) {
			/* ssrxp<->b11, ssrxn<->b10, sstxp<->a2, sstxn<->a3 */
			sw_sel = 0x20;
		}
	} else {
		/* cc2 connection */
		if (mux_type == USB_PD_MUX_DOCK) {
			/* ml0-b10/11, ml1-a2/b3, sstx-b2/a3, ssrx-a10/11 */
			sw_sel = 0x12;
			/* aux+ <-> sbu2, aux- <-> sbu1 */
			aux_sw = 0x0C;
		} else if (mux_type == USB_PD_MUX_DP_ENABLED) {
			/* ml0-b10/11, ml1-a2/b3, ml2-b2/a3, ml3-a10/11 */
			sw_sel = 0x06;
			/* aux+ <-> sbu2, aux- <-> sbu1 */
			aux_sw = 0x0C;
		} else if (mux_type == USB_PD_MUX_USB_ENABLED) {
			/* ssrxp<->a11, ssrxn<->a10, sstxp<->b2, sstxn<->b3 */
			sw_sel = 0x10;
		}
	}

	/*
	 * Once need to configure the Mux, should set the mux to safe mode
	 * first. After the  mux configured, should set mux to normal mode.
	 */
#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
	anx7447_mux_safemode(me, 1);
#endif
	rv = mux_write(me, ANX7447_REG_TCPC_SWITCH_0, sw_sel);
	rv |= mux_write(me, ANX7447_REG_TCPC_SWITCH_1, sw_sel);
	rv |= mux_write(me, ANX7447_REG_TCPC_AUX_SWITCH, aux_sw);

	mux[port].state = mux_state;

#ifdef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD
	/*
	 * DP and Dock mode: after configured the Mux, change the Mux to
	 * normal mode, otherwise: keep safe mode.
	 */
	if (mux_type != USB_PD_MUX_NONE) {
		anx7447_configure_aux_src(me, 1);
		anx7447_mux_safemode(me, 0);
	} else
		anx7447_configure_aux_src(me, 0);
#endif

	return rv;
}

/* current mux state */
static int anx7447_mux_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	int port = me->usb_port;

	*mux_state = mux[port].state;

	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_TCPM_MUX */

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
static int anx7447_tcpc_drp_toggle(int port)
{
	int rv, reg;

	rv = tcpc_read(port, ANX7447_REG_ANALOG_CTRL_10, &reg);
	if (rv)
		return rv;
	/*
	 * When using Look4Connection command to toggle CC under normal mode
	 * the CABLE_DET_DIG shall be clear first.
	 */
	if (reg & ANX7447_REG_CABLE_DET_DIG) {
		reg &= ~ANX7447_REG_CABLE_DET_DIG;
		rv = tcpc_write(port, ANX7447_REG_ANALOG_CTRL_10, reg);
		if (rv)
			return rv;
	}

	return tcpci_tcpc_drp_toggle(port);
}
#endif

/* Override for tcpci_tcpm_set_cc */
static int anx7447_set_cc(int port, int pull)
{
	int rp, reg;

	rp = tcpc_read(port, ANX7447_REG_ANALOG_CTRL_10, &reg);
	if (rp)
		return rp;
	/*
	 * When setting CC status, should be confirm that the CC toggling
	 * process is stopped, the CABLE_DET_DIG shall be set to one.
	 */
	if ((reg & ANX7447_REG_CABLE_DET_DIG) == 0) {
		reg |= ANX7447_REG_CABLE_DET_DIG;
		rp = tcpc_write(port, ANX7447_REG_ANALOG_CTRL_10, reg);
		if (rp)
			return rp;
	}

	rp = tcpci_get_cached_rp(port);

	/* Set manual control, and set both CC lines to the same pull */
	return tcpc_write(port, TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(0, rp, pull, pull));
}

#ifdef CONFIG_CMD_TCPC_DUMP
static const struct tcpc_reg_dump_map anx7447_regs[] = {
	{
		.addr = ANX7447_REG_TCPC_SWITCH_0,
		.name = "SWITCH_0",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_TCPC_SWITCH_1,
		.name = "SWITCH_1",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_TCPC_AUX_SWITCH,
		.name = "AUX_SWITCH",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_ADC_CTRL_1,
		.name = "ADC_CTRL_1",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_ANALOG_CTRL_8,
		.name = "ANALOG_CTRL_8",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_ANALOG_CTRL_10,
		.name = "ANALOG_CTRL_10",
		.size = 1,
	},
	{
		.addr = ANX7447_REG_TCPC_CTRL_2,
		.name = "TCPC_CTRL_2",
		.size = 1,
	},
};

const struct {
	const char *name;
	uint8_t addr;
} anx7447_alt_regs[] = {
	{
		.name = "HPD_CTRL_0",
		.addr = ANX7447_REG_HPD_CTRL_0,
	},
	{
		.name = "HPD_DEGLITCH_H",
		.addr = ANX7447_REG_HPD_DEGLITCH_H,
	},
	{
		.name = "INTP_SOURCE_0",
		.addr = ANX7447_REG_INTP_SOURCE_0,
	},
	{
		.name = "INTP_MASK_0",
		.addr = ANX7447_REG_INTP_MASK_0,
	},
	{
		.name = "INTP_CTRL_0",
		.addr = ANX7447_REG_INTP_CTRL_0,
	},
	{
		.name = "PAD_INTP_CTRL",
		.addr = ANX7447_REG_PAD_INTP_CTRL,
	},
	{
		.name = "OCM_MAIN_VERSION",
		.addr = ANX7447_REG_OCM_MAIN_VERSION,
	},
	{
		.name = "OCM_BUILD_VERSION",
		.addr = ANX7447_REG_OCM_BUILD_VERSION,
	},
};

/*
 * Dump registers for debug command.
 */
static void anx7447_dump_registers(int port)
{
	int i, val;

	tcpc_dump_std_registers(port);
	tcpc_dump_registers(port, anx7447_regs, ARRAY_SIZE(anx7447_regs));
	for (i = 0; i < ARRAY_SIZE(anx7447_alt_regs); i++) {
		anx7447_reg_read(port, anx7447_alt_regs[i].addr, &val);
		ccprintf("  %-26s(ALT/0x%02x) =   0x%02x\n",
			 anx7447_alt_regs[i].name, anx7447_alt_regs[i].addr,
			 (uint8_t)val);
		cflush();
	}
}
#endif /* defined(CONFIG_CMD_TCPC_DUMP) */

static int anx7447_get_chip_info(int port, int live,
				 struct ec_response_pd_chip_info_v1 *chip_info)
{
	int main_version = 0x0, build_version = 0x0;

	RETURN_ERROR(tcpci_get_chip_info(port, live, chip_info));

	if (chip_info == NULL)
		return EC_SUCCESS;

	if (chip_info->fw_version_number == -1 || live) {
		/*
		 * Before reading ANX7447 SPI target address 0x7e for
		 * new added FW version, need to read ANX7447 I2c
		 * target address 0x58 first to wake up ANX7447.
		 */
		tcpc_read(port, ANX7447_REG_OCM_MAIN_VERSION, &main_version);

		RETURN_ERROR(anx7447_reg_read(
			port, ANX7447_REG_OCM_MAIN_VERSION, &main_version));
		RETURN_ERROR(anx7447_reg_read(
			port, ANX7447_REG_OCM_BUILD_VERSION, &build_version));
	}

	chip_info->fw_version_number = (main_version << 8) | build_version;

	/* The minimum OCM firmware version to support FRS. */
	if (IS_ENABLED(CONFIG_USB_PD_FRS))
		chip_info->min_req_fw_version_number = 0x0115;

	return EC_SUCCESS;
}

enum ec_error_list anx7447_set_bist_test_mode(const int port, const bool enable)
{
	/*
	 * Set CC debounce type as millisecond if enable BIST mode,
	 * otherwise microsecond
	 */
	RETURN_ERROR(tcpc_update8(port, ANX7447_REG_TCPC_CTRL_1, CC_DEBOUNCE_MS,
				  enable ? MASK_SET : MASK_CLR));
	/*
	 * Set CC debounce time to 2ms if enable BIST mode,
	 * otherwise set debounce time to 10us
	 */
	RETURN_ERROR(tcpc_write(port, ANX7447_REG_CC_DEBOUNCE_TIME,
				enable ? 2 : 10));

	anx7447_bist_test_mode[port] = enable;

	return EC_SUCCESS;
}

enum ec_error_list anx7447_get_bist_test_mode(const int port, bool *enable)
{
	*enable = anx7447_bist_test_mode[port];

	return EC_SUCCESS;
}

/*
 * ANX7447 is a TCPCI compatible port controller, with some caveats.
 * It seems to require both CC lines to be set always, instead of just
 * one at a time, according to TCPCI spec.  Thus, now that the TCPCI
 * driver more closely follows the spec, this driver requires
 * overrides for set_cc and set_polarity.
 */
const struct tcpm_drv anx7447_tcpm_drv = {
	.init = &anx7447_init,
	.release = &anx7447_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	/*
	 * b:214893572#comment33: ANX7447 dev_cap_1 reports VBUS_MEASURE
	 * unsupported, however, it actually does.
	 */
	.get_vbus_voltage = &tcpci_get_vbus_voltage_no_check,
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &anx7447_set_cc,
	.set_polarity = &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &tcpci_tcpm_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &anx7447_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = anx7447_tcpc_drp_toggle,
#endif
	.get_chip_info = &anx7447_get_chip_info,
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
	.get_snk_ctrl = &tcpci_tcpm_get_snk_ctrl,
	.get_src_ctrl = &tcpci_tcpm_get_src_ctrl,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &anx7447_tcpc_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable = &anx7447_set_frs_enable,
#endif
	.set_bist_test_mode = &anx7447_set_bist_test_mode,
	.get_bist_test_mode = &anx7447_get_bist_test_mode,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers = &anx7447_dump_registers,
#endif
};

#ifdef CONFIG_USB_PD_TCPM_MUX
const struct usb_mux_driver anx7447_usb_mux_driver = {
	.init = anx7447_mux_init,
	.set = anx7447_mux_set,
	.get = anx7447_mux_get,
	.enter_low_power_mode = &tcpci_tcpm_mux_enter_low_power,
};
#endif /* CONFIG_USB_PD_TCPM_MUX */
