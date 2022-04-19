/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush family-specific USB-C configuration */

#include "cros_board_info.h"
#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state_v2.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/aoz1380.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/anx7491.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/anx7451.h"
#include "driver/usb_mux/amd_fp6.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* USB-A ports */
enum usba_port {
	USBA_PORT_A0 = 0,
	USBA_PORT_A1,
	USBA_PORT_COUNT
};

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

static void reset_nct38xx_port(int port);

const struct tcpc_config_t tcpc_config[] = {
	[USBC_PORT_C0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	[USBC_PORT_C1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		},
		.drv = &nct38xx_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

static void usbc_interrupt_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_tcpc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_tcpc));

	/* Enable BC 1.2 interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));

	/* TODO: Enable SBU fault interrupts (io expander )*/
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		/* Device does not talk I2C */
		.drv = &aoz1380_drv
	},

	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NX20P3483_ADDR1_FLAGS,
		.drv = &nx20p348x_drv
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == CONFIG_USB_PD_PORT_MAX_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
static int fsusb42umx_set_mux(const struct usb_mux*, mux_state_t, bool *);
struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = fsusb42umx_set_mux,
};

/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
struct usb_mux usbc0_sbu_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &usbc0_sbu_mux_driver,
};

__overridable int board_c1_ps8818_mux_set(const struct usb_mux *me,
					  mux_state_t mux_state)
{
	CPRINTSUSB("C1: PS8818 mux using default tuning");
	return 0;
}

struct usb_mux usbc1_ps8818 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.flags = USB_MUX_FLAG_RESETS_IN_G3,
	.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &board_c1_ps8818_mux_set,
};

/*
 * ANX7491(A1) and ANX7451(C1) are on the same i2c bus. Both default
 * to 0x29 for the USB i2c address. This moves ANX7451(C1) USB i2c
 * address to 0x2A. ANX7491(A1) will stay at the default 0x29.
 */
uint16_t board_anx7451_get_usb_i2c_addr(const struct usb_mux *me)
{
	ASSERT(me->usb_port == USBC_PORT_C1);
	return 0x2a;
}

__overridable int board_c1_anx7451_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	CPRINTSUSB("C1: ANX7451 mux using default tuning");
	return 0;
}

struct usb_mux usbc1_anx7451 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.flags = USB_MUX_FLAG_RESETS_IN_G3,
	.i2c_addr_flags = ANX7491_I2C_ADDR3_FLAGS,
	.driver = &anx7451_usb_mux_driver,
	.board_set = &board_c1_anx7451_mux_set,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = AMD_FP6_C0_MUX_I2C_ADDR,
		.driver = &amd_fp6_usb_mux_driver,
		.next_mux = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_USB_MUX,
		.i2c_addr_flags = AMD_FP6_C4_MUX_I2C_ADDR,
		.driver = &amd_fp6_usb_mux_driver,
		/* .next_mux = filled in by setup_mux based on fw_config */
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it needs a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			      bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);

	return EC_SUCCESS;
}

static void setup_mux(void)
{
	/* TODO: Fill in C1 mux based on CBI */
	CPRINTSUSB("C1: Setting ANX7451 mux");
	usb_muxes[USBC_PORT_C1].next_mux = &usbc1_anx7451;
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			     port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int rv;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * If this port had booted in dead battery mode, go
			 * ahead and reset it so EN_SNK responds properly.
			 */
			if (nct38xx_get_boot_type(i) ==
						NCT38XX_BOOT_DEAD_BATTERY) {
				reset_nct38xx_port(i);
				pd_set_error_recovery(i);
			}

			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/*
	 * Check if we can reset any ports in dead battery mode
	 *
	 * The NCT3807 may continue to keep EN_SNK low on the dead battery port
	 * and allow a dangerous level of voltage to pass through to the initial
	 * charge port (see b/183660105).  We must reset the ports if we have
	 * sufficient battery to do so, which will bring EN_SNK back under
	 * normal control.
	 */
	rv = EC_SUCCESS;
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (nct38xx_get_boot_type(i) == NCT38XX_BOOT_DEAD_BATTERY) {
			CPRINTSUSB("Found dead battery on %d", i);
			/*
			 * If we have battery, get this port reset ASAP.
			 * This means temporarily rejecting charge manager
			 * sets to it.
			 */
			if (pd_is_battery_capable()) {
				reset_nct38xx_port(i);
				pd_set_error_recovery(i);

				if (port == i)
					rv = EC_ERROR_INVAL;
			} else if (port != i) {
				/*
				 * If other port is selected and in dead battery
				 * mode, reset this port.  Otherwise, reject
				 * change because we'll brown out.
				 */
				if (nct38xx_get_boot_type(port) ==
						NCT38XX_BOOT_DEAD_BATTERY) {
					reset_nct38xx_port(i);
					pd_set_error_recovery(i);
				} else {
					rv = EC_ERROR_INVAL;
				}
			}
		}
	}

	if (rv != EC_SUCCESS)
		return rv;

	/* Check if the port is sourcing VBUS. */
	if (tcpm_get_src_ctrl(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTSUSB("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
int board_aoz1380_set_vbus_source_current_limit(int port,
						enum tcpc_rp_value rp)
{
	int rv = EC_SUCCESS;

	rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

/* TODO: sbu_fault_interrupt from io expander */

/* Round up 3250 max current to multiple of 128mA for ISL9241 AC prochot. */
#define GUYBRUSH_AC_PROCHOT_CURRENT_MA 3328
static void set_ac_prochot(void)
{
	isl9241_set_ac_prochot(CHARGER_SOLO, GUYBRUSH_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, set_ac_prochot, HOOK_PRIO_DEFAULT);

void tcpc_alert_event(enum gpio_signal signal)
{
	int port;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;

	/* TODO: Save and restore ioex signals */
	if (port == USBC_PORT_C0)
		reset_gpio_l = GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst_l);
	else if (port == USBC_PORT_C1)
		reset_gpio_l = GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_rst_l);
	else
		/* Invalid port: do nothing */
		return;

	gpio_pin_set_dt(reset_gpio_l, 0);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		msleep(NCT3807_RESET_POST_DELAY_MS);
}


void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	reset_nct38xx_port(USBC_PORT_C1);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_pin_get_dt(
	     GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_pin_get_dt(
	     GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_int_odl))) {
		if (gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

/**
 * Return if VBUS is sagging too low
 *
 * For legacy BC1.2 charging with CONFIG_CHARGE_RAMP_SW, ramp up input current
 * until voltage drops to 4.5V. Don't go lower than this to be kind to the
 * charger (see b/67964166).
 */
#define BC12_MIN_VOLTAGE 4500
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage = 0;
	int rv;

	rv = charger_get_vbus_voltage(port, &voltage);

	if (rv) {
		CPRINTSUSB("%s rv=%d", __func__, rv);
		return 0;
	}

	/*
	 * b/168569046: The ISL9241 sometimes incorrectly reports 0 for unknown
	 * reason, causing ramp to stop at 0.5A. Workaround this by ignoring 0.
	 * This partly defeats the point of ramping, but will still catch
	 * VBUS below 4.5V and above 0V.
	 */
	if (voltage == 0) {
		CPRINTSUSB("%s vbus=0", __func__);
		return 0;
	}

	if (voltage < BC12_MIN_VOLTAGE)
		CPRINTSUSB("%s vbus=%d", __func__, voltage);

	return voltage < BC12_MIN_VOLTAGE;
}

#define SAFE_RESET_VBUS_DELAY_MS 900
#define SAFE_RESET_VBUS_MV 5000
void board_hibernate(void)
{
	int port;
	enum ec_error_list ret;

	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851, b/143778351, b/147007265)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE) {
		pd_request_source_voltage(port, SAFE_RESET_VBUS_MV);

		/* Give PD task and PPC chip time to get to 5V */
		msleep(SAFE_RESET_VBUS_DELAY_MS);
	}

	/* Try to put our battery fuel gauge into sleep mode */
	ret = battery_sleep_fuel_gauge();
	if ((ret != EC_SUCCESS) && (ret != EC_ERROR_UNIMPLEMENTED))
		cprints(CC_SYSTEM, "Failed to send battery sleep command");
}

__overridable enum ec_error_list
board_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

static int baseboard_a1_ps8811_retimer_init(const struct usb_mux *me)
{
	int rv;
	int tries = 2;

	do {
		int val;

		rv = ps8811_i2c_read(me, PS8811_REG_PAGE1,
				     PS8811_REG1_USB_BEQ_LEVEL, &val);
	} while (rv && --tries);

	if (rv) {
		CPRINTSUSB("A1: PS8811 retimer not detected!");
		return rv;
	}
	CPRINTSUSB("A1: PS8811 retimer detected");
	rv = board_a1_ps8811_retimer_init(me);
	if (rv)
		CPRINTSUSB("A1: Error during PS8811 setup rv:%d", rv);
	return rv;
}

/*
 * PS8811 is just a type-A USB retimer, reusing mux structure for
 * convenience.
 */
const struct usb_mux usba1_ps8811 = {
	.usb_port = USBA_PORT_A1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8811_I2C_ADDR_FLAGS3,
	.board_init = &baseboard_a1_ps8811_retimer_init,
};

__overridable enum ec_error_list
board_a1_anx7491_retimer_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

static int baseboard_a1_anx7491_retimer_init(const struct usb_mux *me)
{
	int rv;
	int tries = 2;

	do {
		int val;

		rv = i2c_read8(me->i2c_port, me->i2c_addr_flags, 0, &val);
	} while (rv && --tries);
	if (rv) {
		CPRINTSUSB("A1: ANX7491 retimer not detected!");
		return rv;
	}
	CPRINTSUSB("A1: ANX7491 retimer detected");
	rv = board_a1_anx7491_retimer_init(me);
	if (rv)
		CPRINTSUSB("A1: Error during ANX7491 setup rv:%d", rv);
	return rv;
}

/*
 * ANX7491 is just a type-A USB retimer, reusing mux structure for
 * convenience.
 */
const struct usb_mux usba1_anx7491 = {
	.usb_port = USBA_PORT_A1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = ANX7491_I2C_ADDR0_FLAGS,
	.board_init = &baseboard_a1_anx7491_retimer_init,
};

void baseboard_a1_retimer_setup(void)
{
	struct usb_mux a1_retimer;

	/* TODO: Support PS8811 retimer through CBI */
	a1_retimer = usba1_anx7491;
	a1_retimer.board_init(&a1_retimer);
}
DECLARE_DEFERRED(baseboard_a1_retimer_setup);

/* TODO: Remove when guybrush is no longer supported */
#ifdef CONFIG_BOARD_GUYBRUSH
void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USBC_PORT_C0:
	case USBC_PORT_C1:
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_usb_c0_c1_fault_odl),
			!is_overcurrented);
		break;

	default:
		break;
	}
}
#endif
