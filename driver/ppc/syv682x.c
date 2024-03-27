/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Silergy SYV682x USB-C Power Path Controller */
#include "atomic.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "syv682x.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define SYV682X_FLAGS_SOURCE_ENABLED BIT(0)
#define SYV682X_FLAGS_SINK_ENABLED BIT(1)
/* 0 -> CC1, 1 -> CC2 */
#define SYV682X_FLAGS_CC_POLARITY BIT(2)
#define SYV682X_FLAGS_VBUS_PRESENT BIT(3)
#define SYV682X_FLAGS_TSD BIT(4)
#define SYV682X_FLAGS_OVP BIT(5)
#define SYV682X_FLAGS_5V_OC BIT(6)
#define SYV682X_FLAGS_FRS BIT(7)
#define SYV682X_FLAGS_VCONN_OCP BIT(8)

static atomic_t irq_pending; /* Bitmask of ports signaling an interrupt. */
static atomic_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];
/* Running count of sink ocp events */
static atomic_t sink_ocp_count[CONFIG_USB_PD_PORT_MAX_COUNT];
static timestamp_t vbus_oc_timer[CONFIG_USB_PD_PORT_MAX_COUNT];
static timestamp_t vconn_oc_timer[CONFIG_USB_PD_PORT_MAX_COUNT];

#define SYV682X_VBUS_DET_THRESH_MV 4000
/* Longest time that can be programmed in DSG_TIME field */
#define SYV682X_MAX_VBUS_DISCHARGE_TIME_MS 400
/*
 * Delay between checks when polling the interrupt registers. Must be longer
 * than the HW deglitch on OC (10ms)
 */
#define INTERRUPT_DELAY_MS 15
/* Deglitch in ms of sourcing overcurrent detection */
#define SOURCE_OC_DEGLITCH_MS 100
#define VCONN_OC_DEGLITCH_MS 100
/* Max. number of OC events allowed before disabling port */
#define OCP_COUNT_LIMIT 3

#if INTERRUPT_DELAY_MS <= SYV682X_HW_OC_DEGLITCH_MS
#error "INTERRUPT_DELAY_MS should be greater than SYV682X_HW_OC_DEGLITCH_MS"
#endif

#if SOURCE_OC_DEGLITCH_MS < INTERRUPT_DELAY_MS
#error "SOURCE_OC_DEGLITCH_MS should be at least INTERRUPT_DELAY_MS"
#endif

/* When FRS is enabled, the VCONN line isn't passed through to the TCPC */
#if defined(CONFIG_USB_PD_FRS_PPC) && defined(CONFIG_USBC_VCONN) && \
	!defined(CONFIG_USBC_PPC_VCONN)
#error "if FRS is enabled on the SYV682X, VCONN must be supplied by the PPC " \
	"instead of the TCPC"
#endif

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static int syv682x_vbus_sink_enable(int port, int enable);

static int syv682x_init(int port);

static void syv682x_interrupt_delayed(int port, int delay);

static int read_reg(uint8_t port, int reg, int *regval)
{
	return i2c_read8(ppc_chips[port].i2c_port,
			 ppc_chips[port].i2c_addr_flags, reg, regval);
}
#ifdef CONFIG_USBC_PPC_SYV682X_OVP_SET_15V
static const int ovp_val = SYV682X_OVP_17_9;
#else
static const int ovp_val = SYV682X_OVP_23_7;
#endif

#ifdef CONFIG_USBC_PPC_SYV682C
__overridable int syv682x_board_is_syv682c(int port)
{
	return true;
}
#endif

/*
 * During channel transition or discharge, the SYV682X silently ignores I2C
 * writes. Poll the BUSY bit until the SYV682A is ready.
 */
static int syv682x_wait_for_ready(int port, int reg)
{
	int regval;
	int rv;
	timestamp_t deadline;

#ifdef CONFIG_USBC_PPC_SYV682C
	/* On SYV682C, busy bit is not applied to CONTROL_4 */
	if (syv682x_board_is_syv682c(port) && reg == SYV682X_CONTROL_4_REG)
		return EC_SUCCESS;
#endif

	deadline.val =
		get_time().val + (SYV682X_MAX_VBUS_DISCHARGE_TIME_MS * MSEC);

	do {
		rv = read_reg(port, SYV682X_CONTROL_3_REG, &regval);
		if (rv)
			return rv;

		if (!(regval & SYV682X_BUSY))
			break;

		if (timestamp_expired(deadline, NULL)) {
			ppc_prints("busy timeout", port);
			return EC_ERROR_TIMEOUT;
		}

		msleep(1);
	} while (1);

	return EC_SUCCESS;
}

static int write_reg(uint8_t port, int reg, int regval)
{
	int rv;

	rv = syv682x_wait_for_ready(port, reg);
	if (rv)
		return rv;

	return i2c_write8(ppc_chips[port].i2c_port,
			  ppc_chips[port].i2c_addr_flags, reg, regval);
}

static int syv682x_is_sourcing_vbus(int port)
{
	return !!(flags[port] & SYV682X_FLAGS_SOURCE_ENABLED);
}

static int syv682x_discharge_vbus(int port, int enable)
{
#ifndef CONFIG_USBC_PPC_SYV682X_SMART_DISCHARGE
	int regval;
	int rv;
	/* cached force discharge flag to reduce the call to the discharge
	 * function.
	 */
	static uint8_t sd_flags[CONFIG_USB_PD_PORT_MAX_COUNT] = {
		[0 ... CONFIG_USB_PD_PORT_MAX_COUNT - 1] = 0xFF
	};

	if ((!!enable) == sd_flags[port])
		return EC_SUCCESS;

	rv = read_reg(port, SYV682X_CONTROL_2_REG, &regval);
	if (rv)
		return rv;

	if (enable)
		regval |= SYV682X_CONTROL_2_FDSG;
	else
		regval &= ~SYV682X_CONTROL_2_FDSG;

	rv = write_reg(port, SYV682X_CONTROL_2_REG, regval);

	if (!rv)
		sd_flags[port] = !!enable;

	return rv;
#else
	/*
	 * Smart discharge mode is enabled, nothing to do
	 */
	return EC_SUCCESS;
#endif
}

static int syv682x_vbus_source_enable(int port, int enable)
{
	int regval;
	int rv;
	/*
	 * For source mode need to make sure 5V power path is connected
	 * and source mode is selected.
	 */
	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	if (enable) {
		/* Select 5V path and turn on channel */
		regval &=
			~(SYV682X_CONTROL_1_CH_SEL | SYV682X_CONTROL_1_PWR_ENB);
		/* Disable HV Sink path */
		regval |= SYV682X_CONTROL_1_HV_DR;
	} else if (flags[port] & SYV682X_FLAGS_SOURCE_ENABLED) {
		/*
		 * For the disable case, make sure that VBUS was being sourced
		 * prior to disabling the source path. Because the source/sink
		 * paths can't be independently disabled, and this function will
		 * get called as part of USB PD initialization, setting the
		 * PWR_ENB always can lead to broken dead battery behavior.
		 *
		 * No need to change the voltage path or channel direction. But,
		 * turn both paths off.
		 *
		 * De-assert the FRS GPIO, which will be asserted if we got to
		 * be a source via an FRS.
		 */
		regval |= SYV682X_CONTROL_1_PWR_ENB;
		if (IS_ENABLED(CONFIG_USB_PD_FRS_PPC))
			gpio_or_ioex_set_level(ppc_chips[port].frs_en, 0);
	}

	rv = write_reg(port, SYV682X_CONTROL_1_REG, regval);
	if (rv)
		return rv;

	if (enable) {
		atomic_or(&flags[port], SYV682X_FLAGS_SOURCE_ENABLED);
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SINK_ENABLED);
	} else {
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SOURCE_ENABLED);
	}

#if defined(CONFIG_USB_CHARGER) && defined(CONFIG_USB_PD_VBUS_DETECT_PPC)
	/*
	 * Since the VBUS state could be changing here, need to wake the
	 * USB_CHG_N task so that BC 1.2 detection will be triggered.
	 */
	usb_charger_vbus_change(port, enable);
#endif

	return EC_SUCCESS;
}

/* Filter interrupts with rising edge trigger */
static bool syv682x_interrupt_filter(int port, int regval, int regmask,
				     int flagmask)
{
	if (regval & regmask) {
		if (!(flags[port] & flagmask)) {
			atomic_or(&flags[port], flagmask);
			return true;
		}
	} else {
		atomic_clear_bits(&flags[port], flagmask);
	}
	return false;
}

#ifdef CONFIG_USB_PD_FRS_PPC
#define CC_RP_DEBOUNCE 1000
/*
 * According to the syv682 manual, the FRS process of SYV682 only determines
 * Rd pull down. Unplugging the dock may trigger FRS. Base on USB PD 3.2 spec,
 * Version 1.0, Sections 8.3.2.9. The source port drives CC to ground for
 * no larger than tFRSwapTx(MAX). In order to avoid FRS errors in syv682,
 * add CC status judgment after FRS triggered.
 */
static int check_cc_rp_timeout(int port, int timeout)
{
#ifdef CONFIG_ZTEST
	return EC_SUCCESS;
#endif
	enum tcpc_cc_voltage_status cc1, cc2;

	tcpm_get_cc(port, &cc1, &cc2);

	while (((cc_is_rp(cc1)) || (cc_is_rp(cc2))) != true) {
		if (task_wait_event(timeout) == TASK_EVENT_TIMER) {
			return EC_ERROR_TIMEOUT;
		}
		tcpm_get_cc(port, &cc1, &cc2);
	}

	return EC_SUCCESS;
}
#endif

/*
 * Two status registers can trigger the ALERT_L pin, STATUS and CONTROL_4
 * These registers are clear on read if the condition has been cleared.
 * The ALERT_L pin will not de-assert if the alert condition has not been
 * cleared. Since they are clear on read, we should check the alerts whenever we
 * read these registers to avoid race conditions.
 */
static void syv682x_handle_status_interrupt(int port, int regval)
{
#ifdef CONFIG_USB_PD_FRS_PPC
	/*
	 * An FRS will automatically disable sinking immediately, and enable the
	 * source path if VBUS is <5V. The FRS GPIO must remain asserted until
	 * VBUS falls below 5V. SYV682X_FLAGS_FRS signals that the SRC state was
	 * entered via an FRS.
	 *
	 * Note the FRS Alert will remain asserted until VBUS has fallen below
	 * 5V or the frs_en gpio is de-asserted. So use the rising edge trigger.
	 */
	if (syv682x_interrupt_filter(port, regval, SYV682X_STATUS_FRS,
				     SYV682X_FLAGS_FRS)) {
		/* Add CC status judgment after FRS trigger. */
		if (check_cc_rp_timeout(port, CC_RP_DEBOUNCE)) {
			pd_set_error_recovery(port);
			return;
		}

		atomic_or(&flags[port], SYV682X_FLAGS_SOURCE_ENABLED);
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SINK_ENABLED);
		if (!tcpm_tcpc_has_frs_control(port))
			pd_got_frs_signal(port);
	}
#endif

	/*
	 * 5V OC is actually notifying that it is current limiting
	 * to 3.3A. If this happens for a long time, we will trip TSD
	 * which will disable the channel. We should disable the sourcing path
	 * before that happens for safety reasons.
	 *
	 * On first check, set the flag and set the timer. This also clears the
	 * flag if the OC is gone.
	 */
	if (syv682x_interrupt_filter(port, regval, SYV682X_STATUS_OC_5V,
				     SYV682X_FLAGS_5V_OC)) {
		vbus_oc_timer[port].val =
			get_time().val + SOURCE_OC_DEGLITCH_MS * MSEC;
	} else if ((regval & SYV682X_STATUS_OC_5V) &&
		   (get_time().val > vbus_oc_timer[port].val)) {
		vbus_oc_timer[port].val = UINT64_MAX;
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_5V_OC);
		syv682x_vbus_source_enable(port, 0);
		pd_handle_overcurrent(port);
	}

	/*
	 * No PD handling for VBUS OVP or TSD events.
	 * For TSD, this means we are in danger of burning the device so turn
	 *   everything off and leave it off. The power paths will be
	 *   automatically disabled.
	 * In the case of OVP, the channels will be
	 *   disabled but don't unset the sink flag, since a sink OCP can
	 *   inadvertently cause an OVP, and we'd want to re-enable the sink
	 *   path in that situation.
	 */
	if (syv682x_interrupt_filter(port, regval, SYV682X_STATUS_TSD,
				     SYV682X_FLAGS_TSD)) {
		ppc_prints("TSD!", port);
		atomic_clear_bits(&flags[port],
				  SYV682X_FLAGS_SOURCE_ENABLED |
					  SYV682X_FLAGS_SINK_ENABLED);
	}
	if (syv682x_interrupt_filter(port, regval, SYV682X_STATUS_OVP,
				     SYV682X_FLAGS_OVP)) {
		ppc_prints("VBUS OVP!", port);
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SOURCE_ENABLED);
	}

	/*
	 * HV OC is a hard limit that will disable the sink path (automatically
	 * removing this alert condition), so try re-enabling if we hit an OCP.
	 * If we get multiple OCPs, don't re-enable. The OCP counter is reset on
	 * the sink path being explicitly disabled or on a PPC init.
	 */
	if (regval & SYV682X_STATUS_OC_HV) {
		ppc_prints("Sink OCP!", port);
		atomic_add(&sink_ocp_count[port], 1);
		if ((sink_ocp_count[port] < OCP_COUNT_LIMIT) &&
		    (flags[port] & SYV682X_FLAGS_SINK_ENABLED)) {
			syv682x_vbus_sink_enable(port, 1);
		} else {
			ppc_prints("Disable sink", port);
			atomic_clear_bits(&flags[port],
					  SYV682X_FLAGS_SINK_ENABLED);
		}
	}
}

static int syv682x_handle_control_4_interrupt(int port, int regval)
{
	/*
	 * VCONN OC is actually notifying that it is current limiting
	 * to 600mA. If this happens for a long time, we will trip TSD
	 * which will disable the channel. We should disable the sourcing path
	 * before that happens for safety reasons.
	 *
	 * On first check, set the flag and set the timer. This also clears the
	 * flag if the OC is gone.
	 */
	if (syv682x_interrupt_filter(port, regval, SYV682X_CONTROL_4_VCONN_OCP,
				     SYV682X_FLAGS_VCONN_OCP)) {
		vconn_oc_timer[port].val =
			get_time().val + VCONN_OC_DEGLITCH_MS * MSEC;
	} else if ((regval & SYV682X_CONTROL_4_VCONN_OCP) &&
		   (get_time().val > vconn_oc_timer[port].val)) {
		vconn_oc_timer[port].val = UINT64_MAX;
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_VCONN_OCP);

		/* Disable VCONN */
		regval &=
			~(SYV682X_CONTROL_4_VCONN2 | SYV682X_CONTROL_4_VCONN1);
		write_reg(port, SYV682X_CONTROL_4_REG, regval);

		ppc_prints("VCONN OC!", port);
	}

	/*
	 * On VBAT OVP, CC/VCONN are cut. Re-enable before sending the hard
	 * reset using a PPC re-init. We could reconfigure CC based on flags,
	 * but these will be updated anyway due to a hard reset so just re-init
	 * for simplicity. If this happens return an error since this isn't
	 * recoverable.
	 */
	if (regval & SYV682X_CONTROL_4_VBAT_OVP) {
		ppc_prints("VBAT or CC OVP!", port);
		syv682x_init(port);
		pd_handle_cc_overvoltage(port);
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int syv682x_vbus_sink_enable(int port, int enable)
{
	int regval;
	int rv;

	if (!enable) {
		atomic_clear(&sink_ocp_count[port]);
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SINK_ENABLED);
		/*
		 * We're currently a source, so nothing more to do
		 */
		if (syv682x_is_sourcing_vbus(port))
			return EC_SUCCESS;
	} else if (sink_ocp_count[port] > OCP_COUNT_LIMIT) {
		/*
		 * Don't re-enable the channel until an explicit sink disable
		 * resets the ocp counter.
		 */
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * For sink mode need to make sure high voltage power path is connected
	 * and sink mode is selected.
	 */
	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	if (enable) {
		/* Select high voltage path */
		regval |= SYV682X_CONTROL_1_CH_SEL;
		/* Select Sink mode and turn on the channel */
		regval &=
			~(SYV682X_CONTROL_1_HV_DR | SYV682X_CONTROL_1_PWR_ENB);
		/* Set sink current limit to the configured value */
		regval |= CONFIG_SYV682X_HV_ILIM << SYV682X_HV_ILIM_BIT_SHIFT;
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_SOURCE_ENABLED);
		atomic_or(&flags[port], SYV682X_FLAGS_SINK_ENABLED);
	} else {
		/*
		 * No need to change the voltage path or channel direction. But,
		 * turn both paths off because we are currently a sink.
		 */
		regval |= SYV682X_CONTROL_1_PWR_ENB;
	}

	return write_reg(port, SYV682X_CONTROL_1_REG, regval);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int syv682x_is_vbus_present(int port)
{
	int val;
	int vbus = 0;

	if (read_reg(port, SYV682X_STATUS_REG, &val))
		return vbus;
	/*
	 * The status register interrupt bits are clear on read, check
	 * register value to see if there are interrupts to avoid race
	 * conditions with the interrupt handler
	 */
	syv682x_handle_status_interrupt(port, val);

	/*
	 * VBUS is considered present if VSafe5V is detected or neither VSafe5V
	 * or VSafe0V is detected, which implies VBUS > 5V.
	 */
	if ((val & SYV682X_STATUS_VSAFE_5V) ||
	    !(val & (SYV682X_STATUS_VSAFE_5V | SYV682X_STATUS_VSAFE_0V)))
		vbus = 1;
#ifdef CONFIG_USB_CHARGER
	if (!!(flags[port] & SYV682X_FLAGS_VBUS_PRESENT) != vbus)
		usb_charger_vbus_change(port, vbus);

	if (vbus)
		atomic_or(&flags[port], SYV682X_FLAGS_VBUS_PRESENT);
	else
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_VBUS_PRESENT);
#endif

	return vbus;
}
#endif

static int syv682x_set_vbus_source_current_limit(int port,
						 enum tcpc_rp_value rp)
{
	int rv;
	int limit;
	int regval;

	rv = read_reg(port, SYV682X_CONTROL_1_REG, &regval);
	if (rv)
		return rv;

	/* We need buffer room for all current values. */
	switch (rp) {
	case TYPEC_RP_3A0:
		limit = SYV682X_5V_ILIM_3_30;
		break;

	case TYPEC_RP_1A5:
		limit = SYV682X_5V_ILIM_1_75;
		break;

	case TYPEC_RP_USB:
	default:
		/* 1.25 A is lowest current limit setting for SVY682 */
		limit = SYV682X_5V_ILIM_1_25;
		break;
	};

	regval &= ~SYV682X_5V_ILIM_MASK;
	regval |= (limit << SYV682X_5V_ILIM_BIT_SHIFT);
	return write_reg(port, SYV682X_CONTROL_1_REG, regval);
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int syv682x_set_polarity(int port, int polarity)
{
	/*
	 * The SYV682x does not explicitly set CC polarity. However, if VCONN is
	 * being used then the polarity is required to connect 5V to the correct
	 * CC line. So this function saves the CC polarity as a bit in the flags
	 * variable so VCONN is connected the correct CC line. The flag bit
	 * being set means polarity = CC2, the flag bit clear means
	 * polarity = CC1.
	 */
	if (polarity)
		atomic_or(&flags[port], SYV682X_FLAGS_CC_POLARITY);
	else
		atomic_clear_bits(&flags[port], SYV682X_FLAGS_CC_POLARITY);

	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_USBC_PPC_VCONN
static int syv682x_set_vconn(int port, int enable)
{
	int regval;
	int rv;

	rv = read_reg(port, SYV682X_CONTROL_4_REG, &regval);
	if (rv)
		return rv;
	/*
	 * The control4 register interrupt bits are clear on read, check
	 * register value to see if there are interrupts to avoid race
	 * conditions with the interrupt handler
	 */
	rv = syv682x_handle_control_4_interrupt(port, regval);
	if (rv)
		return rv;

	regval &= ~(SYV682X_CONTROL_4_VCONN2 | SYV682X_CONTROL_4_VCONN1);
	if (enable) {
		regval |= flags[port] & SYV682X_FLAGS_CC_POLARITY ?
				  SYV682X_CONTROL_4_VCONN1 :
				  SYV682X_CONTROL_4_VCONN2;
	}

	return write_reg(port, SYV682X_CONTROL_4_REG, regval);
}
#endif

#ifdef CONFIG_CMD_PPC_DUMP
static int syv682x_dump(int port)
{
	int reg_addr;
	int data;
	int rv;
	const int i2c_port = ppc_chips[port].i2c_port;
	const int i2c_addr_flags = ppc_chips[port].i2c_addr_flags;

	for (reg_addr = SYV682X_STATUS_REG; reg_addr <= SYV682X_CONTROL_4_REG;
	     reg_addr++) {
		rv = i2c_read8(i2c_port, i2c_addr_flags, reg_addr, &data);
		if (rv)
			ccprintf("ppc_syv682[p%d]: Failed to read reg 0x%02x\n",
				 port, reg_addr);
		else
			ccprintf("ppc_syv682[p%d]: reg 0x%02x = 0x%02x\n", port,
				 reg_addr, data);
	}

	cflush();

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

static void syv682x_handle_interrupt(int port)
{
	int control4;
	int status;

	/* Both interrupt registers are clear on read */
	read_reg(port, SYV682X_CONTROL_4_REG, &control4);
	syv682x_handle_control_4_interrupt(port, control4);

	read_reg(port, SYV682X_STATUS_REG, &status);
	syv682x_handle_status_interrupt(port, status);

	/*
	 * Since ALERT_L is level-triggered, check the alert status and repeat
	 * until all interrupts are cleared. The SYV682B and later have a 10ms
	 * deglitch on OC, so make sure not to check the status register again
	 * for at least 10ms to give it time to re-trigger. This will not spam
	 * indefinitely on OCP, but may on OVP, RVS, or TSD.
	 */

	if (status & SYV682X_STATUS_INT_MASK ||
	    control4 & SYV682X_CONTROL_4_INT_MASK) {
		syv682x_interrupt_delayed(port, INTERRUPT_DELAY_MS);
	}
}

static void syv682x_irq_deferred(void)
{
	int i;
	uint32_t pending = atomic_clear(&irq_pending);

	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		if (BIT(i) & pending)
			syv682x_handle_interrupt(i);
}
DECLARE_DEFERRED(syv682x_irq_deferred);

static void syv682x_interrupt_delayed(int port, int delay)
{
	atomic_or(&irq_pending, BIT(port));
	hook_call_deferred(&syv682x_irq_deferred_data, delay * MSEC);
}

test_mockable void syv682x_interrupt(int port)
{
	/* FRS timings require <15ms response to an FRS event */
	syv682x_interrupt_delayed(port, 0);
}

/*
 * The frs_en signal can be driven from the TCPC as well (preferred).
 * In that case, no PPC configuration needs to be done to enable FRS
 */
#ifdef CONFIG_USB_PD_FRS_PPC
static int syv682x_set_frs_enable(int port, int enable)
{
	int regval;

	read_reg(port, SYV682X_CONTROL_4_REG, &regval);
	syv682x_handle_control_4_interrupt(port, regval);

	if (enable) {
		/*
		 * The CC line is the FRS trigger, and VCONN should
		 * be ignored. The SYV682 uses the CCx_BPS fields to
		 * determine if CC1 or CC2 is CC and should be used for
		 * FRS. This CCx is also connected through to the TCPC.
		 * The other CCx signal (VCONN) is isolated from the
		 * TCPC with this write (VCONN must be provided by PPC).
		 *
		 * It is not a valid state to have both or neither
		 * CC_BPS bits set and the CC_FRS enabled, exactly 1
		 * should be set.
		 */
		regval &= ~(SYV682X_CONTROL_4_CC1_BPS |
			    SYV682X_CONTROL_4_CC2_BPS);
		regval |= flags[port] & SYV682X_FLAGS_CC_POLARITY ?
				  SYV682X_CONTROL_4_CC2_BPS :
				  SYV682X_CONTROL_4_CC1_BPS;
		/* set GPIO after configuring */
		write_reg(port, SYV682X_CONTROL_4_REG, regval);
		gpio_or_ioex_set_level(ppc_chips[port].frs_en, 1);
	} else {
		/*
		 * Reconnect CC lines to TCPC. Since the FRS GPIO needs to be
		 * asserted until VBUS falls below 5V during an FRS, if
		 * SYV682X_FLAGS_FRS is set then don't deassert it, instead
		 * disable when sourcing is disabled.
		 */
		regval |= SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS;
		write_reg(port, SYV682X_CONTROL_4_REG, regval);
		if (!(flags[port] & SYV682X_FLAGS_FRS))
			gpio_or_ioex_set_level(ppc_chips[port].frs_en, 0);
	}
	return EC_SUCCESS;
}
#endif /*CONFIG_USB_PD_FRS_PPC*/

#ifndef CONFIG_USBC_PPC_SYV682X_SMART_DISCHARGE
static int syv682x_dev_is_connected(int port, enum ppc_device_role dev)
{
	/*
	 * (b:160548079) We disable the smart discharge(SDSG), so we should
	 * turn off the discharge FET if a source is connected.
	 */
	if (dev == PPC_DEV_SRC)
		return syv682x_discharge_vbus(port, 0);
	else if (dev == PPC_DEV_DISCONNECTED)
		return syv682x_discharge_vbus(port, 1);

	return EC_SUCCESS;
}
#endif

static bool syv682x_is_sink(uint8_t control_1)
{
	/*
	 * The SYV682 integrates power paths: 5V and HV (high voltage).
	 * The SYV682 can source either 5V or HV, but only sinks on the HV path.
	 *
	 * PD analyzer without a device connected confirms the SYV682 acts as
	 * a source under these conditions:
	 *	HV_DR && !CH_SEL:	source 5V
	 *	HV_DR && CH_SEL:	source 15V
	 *	!HV_DR && !CH_SEL:	source 5V
	 *
	 * The SYV682 is only a sink when !HV_DR && CH_SEL
	 */
	if (!(control_1 & SYV682X_CONTROL_1_PWR_ENB) &&
	    !(control_1 & SYV682X_CONTROL_1_HV_DR) &&
	    (control_1 & SYV682X_CONTROL_1_CH_SEL))
		return true;

	return false;
}

static bool syv682x_is_vconn_controlled_by_tcpc(int port)
{
	return tcpc_config[port].flags & TCPC_FLAGS_CONTROL_VCONN;
}

static int syv682x_init(int port)
{
	int rv;
	int regval;
	int status, control_1;
	enum tcpc_rp_value initial_current_limit;

	/*
	 * Vconn must be sourced by syv682x. The maximum voltage of HOST_CCx
	 * pin is 3.6V. Vconn source by TCPC may exceed 3.6V and damage syv682x.
	 */
	if (syv682x_is_vconn_controlled_by_tcpc(port)) {
		CPRINTS("ERROR! Vconn MUST NOT be controlled by TCPC");
		return EC_ERROR_INVALID_CONFIG;
	}

	rv = read_reg(port, SYV682X_STATUS_REG, &status);
	if (rv)
		return rv;

	rv = read_reg(port, SYV682X_CONTROL_1_REG, &control_1);
	if (rv)
		return rv;
	atomic_clear(&sink_ocp_count[port]);
	atomic_clear(&flags[port]);

	/*
	 * Disable FRS prior to configuring the power paths
	 */
	if (IS_ENABLED(CONFIG_USB_PD_FRS_PPC))
		gpio_or_ioex_set_level(ppc_chips[port].frs_en, 0);

	if (!syv682x_is_sink(control_1) || (status & SYV682X_STATUS_VSAFE_0V)) {
		/*
		 * Disable both power paths,
		 * set HV_ILIM to board default,
		 * set 5V_ILIM to 1.25A,
		 * set HV direction to sink,
		 * select HV channel.
		 */
		regval = SYV682X_CONTROL_1_PWR_ENB |
			 (CONFIG_SYV682X_HV_ILIM << SYV682X_HV_ILIM_BIT_SHIFT) |
			 /* !SYV682X_CONTROL_1_HV_DR */
			 SYV682X_CONTROL_1_CH_SEL;
		rv = write_reg(port, SYV682X_CONTROL_1_REG, regval);
		if (rv)
			return rv;
	} else {
		/* Dead battery mode, or an existing PD contract is in place */
		rv = syv682x_vbus_sink_enable(port, 1);
		if (rv)
			return rv;
	}

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
	initial_current_limit = CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;
#else
	initial_current_limit = CONFIG_USB_PD_PULLUP;
#endif
	rv = syv682x_set_vbus_source_current_limit(port, initial_current_limit);
	if (rv)
		return rv;

	/*
	 * Set Control Reg 2 to defaults except 50ms smart discharge time.
	 * Note: On SYV682A/B, enable smart discharge would block i2c
	 * transactions for 50ms (discharge time) and this
	 * prevents us from disabling Vconn when stop sourcing Vbus and has
	 * tVconnOff (35ms) timeout.
	 * On SYV682C, we are allowed to access CONTROL4 while the i2c busy.
	 */
	regval = (SYV682X_OC_DELAY_10MS << SYV682X_OC_DELAY_SHIFT) |
		 (SYV682X_DSG_RON_200_OHM << SYV682X_DSG_RON_SHIFT) |
		 (SYV682X_DSG_TIME_50MS << SYV682X_DSG_TIME_SHIFT);

	if (IS_ENABLED(CONFIG_USBC_PPC_SYV682X_SMART_DISCHARGE))
		regval |= SYV682X_CONTROL_2_SDSG;

	rv = write_reg(port, SYV682X_CONTROL_2_REG, regval);
	if (rv)
		return rv;

	/*
	 * Always set the over voltage setting to the maximum to support
	 * sinking from a 20V PD charger. The common PPC code doesn't provide
	 * any hooks for indicating what the currently negotiated voltage is.
	 *
	 * Mask Alerts due to Reverse Voltage.
	 */
	regval = (ovp_val << SYV682X_OVP_BIT_SHIFT) | SYV682X_RVS_MASK;
	rv = write_reg(port, SYV682X_CONTROL_3_REG, regval);
	if (rv)
		return rv;

	/*
	 * Remove Rd and connect CC1/CC2 lines to TCPC
	 * Disable Vconn
	 * Enable CC detection of Fast Role Swap (FRS)
	 */
	regval = SYV682X_CONTROL_4_CC1_BPS | SYV682X_CONTROL_4_CC2_BPS;
	rv = write_reg(port, SYV682X_CONTROL_4_REG, regval);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

const struct ppc_drv syv682x_drv = {
	.init = &syv682x_init,
	.is_sourcing_vbus = &syv682x_is_sourcing_vbus,
	.vbus_sink_enable = &syv682x_vbus_sink_enable,
	.vbus_source_enable = &syv682x_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &syv682x_dump,
#endif /* defined(CONFIG_CMD_PPC_DUMP) */
#ifdef CONFIG_USB_PD_FRS_PPC
	.set_frs_enable = &syv682x_set_frs_enable,
#endif
#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &syv682x_is_vbus_present,
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
	.set_vbus_source_current_limit = &syv682x_set_vbus_source_current_limit,
	.discharge_vbus = &syv682x_discharge_vbus,
#ifndef CONFIG_USBC_PPC_SYV682X_SMART_DISCHARGE
	.dev_is_connected = &syv682x_dev_is_connected,
#endif /* defined(CONFIG_USBC_PPC_SYV682X_SMART_DISCHARGE) */
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &syv682x_set_polarity,
#endif
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &syv682x_set_vconn,
#endif
	.interrupt = &syv682x_interrupt,
};
