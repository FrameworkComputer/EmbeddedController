/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "button.h"
#include "charge_state.h"
#include "cros_board_info.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp5.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "keyboard_8042_sharedlib.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* This I2C moved. Temporarily detect and support the V0 HW. */
int I2C_PORT_BATTERY = I2C_PORT_BATTERY_V1;

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/*****************************************************************************
 * Retimers
 */

static void retimers_on(void)
{
	/* usba retimer power on */
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, retimers_on, HOOK_PRIO_DEFAULT);

static void retimers_off(void)
{
	/* usba retimer power off */
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, retimers_off, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * USB-C
 */

/*
 * USB C0 port SBU mux use standalone PI3USB221
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int pi3usb221_set_mux(const struct usb_mux *me, mux_state_t mux_state,
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

/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
const struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = pi3usb221_set_mux,
};

/*
 * Since PI3USB221 is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux_chain usbc0_sbu_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &usbc0_sbu_mux_driver,
		},
};

struct usb_mux_chain usbc1_amd_fp5_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_USB_AP_MUX,
			.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
			.driver = &amd_fp5_usb_mux_driver,
			.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP,
		},
};

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_AP_MUX,
			.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
			.driver = &amd_fp5_usb_mux_driver,
		},
		.next = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_TCPC1,
			.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG,
			.driver = &ps8743_usb_mux_driver,
		},
		.next = &usbc1_amd_fp5_usb_mux,
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

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
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == USBC_PORT_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_FAULT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		/*
		 * Sensitive only to falling edges; GPIO is configured for both
		 * because this input may be used for HDMI HPD instead.
		 */
		if (!gpio_get_level(signal))
			nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
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

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTFUSB("Skip enable C%d", port);
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

void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USBC_PORT_C0:
		ioex_set_level(IOEX_USB_C0_FAULT_ODL, !is_overcurrented);
		break;

	case USBC_PORT_C1:
		ioex_set_level(IOEX_USB_C1_FAULT_ODL, !is_overcurrented);
		break;

	default:
		break;
	}
}

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
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT == USBC_PORT_COUNT);

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},

	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9201_bc12_chips) == USBC_PORT_COUNT);

static void reset_nct38xx_port(int port)
{
	enum gpio_signal reset_gpio_l;

	if (port == USBC_PORT_C0)
		reset_gpio_l = GPIO_USB_C0_TCPC_RST_L;
	else if (port == USBC_PORT_C1)
		reset_gpio_l = GPIO_USB_C1_TCPC_RST_L;
	else
		/* Invalid port: do nothing */
		return;

	gpio_set_level(reset_gpio_l, 0);
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_set_level(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);
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
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_TCPC_RST_L) != 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_TCPC_RST_L) != 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

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

void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

int board_pd_set_frs_enable(int port, int enable)
{
	int rv = EC_SUCCESS;

	/* Use the TCPC to enable fast switch when FRS included */
	if (port == USBC_PORT_C0) {
		rv = ioex_set_level(IOEX_USB_C0_TCPC_FASTSW_CTL_EN, !!enable);
	} else {
		rv = ioex_set_level(IOEX_USB_C1_TCPC_FASTSW_CTL_EN, !!enable);
	}

	return rv;
}

static void setup_fw_config(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_FAULT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC 1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);

	/* Enable SBU fault interrupts */
	ioex_enable_interrupt(IOEX_USB_C0_SBU_FAULT_ODL);
	ioex_enable_interrupt(IOEX_USB_C1_SBU_FAULT_DB_ODL);

	/*
	 * If keyboard is US2(KB_LAYOUT_1), we need translate right ctrl
	 * to backslash(\|) key.
	 */
	if (ec_config_keyboard_layout() == KB_LAYOUT_1)
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
}
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_NCT3807] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[IOEX_C1_NCT3807] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB,
};

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ 0, 5 }, { 1, 1 }, { 1, 0 }, { 0, 6 },	  { 0, 7 },   { 1, 4 },
	{ 1, 3 }, { 1, 6 }, { 1, 7 }, { 3, 1 },	  { 2, 0 },   { 1, 5 },
	{ 2, 6 }, { 2, 7 }, { 2, 1 }, { 2, 4 },	  { 2, 5 },   { 1, 2 },
	{ 2, 3 }, { 2, 2 }, { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

#define CHARGING_CURRENT_500mA 500

int charger_profile_override(struct charge_state_data *curr)
{
	static int thermal_sensor_temp;
	static int prev_thermal_sensor_temp;
	static int limit_charge;
	static int limit_usbc_power;
	static int limit_usbc_power_backup;
	enum tcpc_rp_value rp;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return 0;

	temp_sensor_read(TEMP_SENSOR_CHARGER, &thermal_sensor_temp);

	if (thermal_sensor_temp > prev_thermal_sensor_temp) {
		if (thermal_sensor_temp > C_TO_K(63))
			limit_usbc_power = 1;

		if (thermal_sensor_temp > C_TO_K(58)) {
			if (curr->state == ST_CHARGE)
				limit_charge = 1;
		}
	} else if (thermal_sensor_temp < prev_thermal_sensor_temp) {
		if (thermal_sensor_temp < C_TO_K(62))
			limit_usbc_power = 0;

		if (thermal_sensor_temp < C_TO_K(57)) {
			if (curr->state == ST_CHARGE)
				limit_charge = 0;
		}
	}

	if (limit_charge)
		curr->requested_current = CHARGING_CURRENT_500mA;
	else
		curr->requested_current = curr->batt.desired_current;

	if (limit_usbc_power != limit_usbc_power_backup) {
		if (limit_usbc_power == 1)
			rp = TYPEC_RP_1A5;
		else
			rp = TYPEC_RP_3A0;

		ppc_set_vbus_source_current_limit(0, rp);
		tcpm_select_rp_value(0, rp);
		pd_update_contract(0);
		limit_usbc_power_backup = limit_usbc_power;
	}

	prev_thermal_sensor_temp = thermal_sensor_temp;

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

__override struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_CHARGER] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(63),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(62),
		}
	},
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
			[EC_TEMP_THRESH_HALT] = C_TO_K(85),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		}
	},
	[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
			[EC_TEMP_THRESH_HALT] = C_TO_K(90),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(83),
		}
	},
};
