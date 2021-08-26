/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mancomb family-specific configuration */

#include "adc.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state_v2.h"
#include "charge_state.h"
#include "charger.h"
#include "chip/npcx/ps2_chip.h"
#include "chip/npcx/pwm_chip.h"
#include "chipset.h"
#include "driver/ppc/aoz1380.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/tdp142.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/usb_mux/amd_fp6.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "keyboard_scan.h"
#include "nct38xx.h"
#include "pi3usb9201.h"
#include "power.h"
#include "pwm.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Power Signal Input List */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_N] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct i2c_port_t i2c_ports[] = {
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C0_SCL,
		.sda = GPIO_EC_I2C_USB_C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_C1_SCL,
		.sda = GPIO_EC_I2C_USB_C1_SDA,
	},
	{
		.name = "usb_hub",
		.port = I2C_PORT_USB_HUB,
		.kbps = 100,
		.scl = GPIO_EC_I2C_USBC_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_MUX_SDA,
	},
	{
		.name = "usb_mux",
		.port = I2C_PORT_USB_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_MUX_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C_CBI_SCL,
		.sda = GPIO_EC_I2C_CBI_SDA,
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SENSOR_SCL,
		.sda = GPIO_EC_I2C_SENSOR_SDA,
	},
	{
		.name = "soc_thermal",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SOC_SIC,
		.sda = GPIO_EC_I2C_SOC_SID,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC Channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ANALOG_PPVAR_PWR_IN_IMON] = {
		.name = "POWER_I",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_MEMORY] = {
		.name = "MEMORY",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	/*  100K/(680K+100K) = 5/39 voltage divider */
	[SNS_PPVAR_PWR_IN] = {
		.name = "POWER_V",
		.input_ch = NPCX_ADC_CH5,
		.factor_mul = (ADC_MAX_VOLT) * 39,
		.factor_div = (ADC_READ_MAX + 1) * 5,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_AMBIENT] = {
		.name = "AMBIENT",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temp Sensors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_SOC,
	},
	[TEMP_SENSOR_MEMORY] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_MEMORY,
	},
	[TEMP_SENSOR_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_AMBIENT,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		/* TODO: Setting fan off to 0 so it's always on */
		.temp_fan_off = C_TO_K(0),
		.temp_fan_max = C_TO_K(70),
	},
	[TEMP_SENSOR_MEMORY] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
	[TEMP_SENSOR_AMBIENT] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
		[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

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

/* A1-A4 are controlled by USB MUX */
const int usb_port_enable[] = {
	IOEX_EN_PP5000_USB_A0_VBUS,
};

static void baseboard_interrupt_init(void)
{
	/* Enable Power Group interrupts. */
	gpio_enable_interrupt(GPIO_PG_GROUPC_S0_OD);
	gpio_enable_interrupt(GPIO_PG_DDR4_S3_OD);

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC 1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);

	/* Enable SBU fault interrupts */
	ioex_enable_interrupt(IOEX_USB_C0_SBU_FAULT_ODL);
	ioex_enable_interrupt(IOEX_USB_C1_SBU_FAULT_ODL);

	/* Enable USB-A fault interrupts */
	gpio_enable_interrupt(GPIO_USB_A4_FAULT_R_ODL);
	gpio_enable_interrupt(GPIO_USB_A3_FAULT_R_ODL);
	gpio_enable_interrupt(GPIO_USB_A2_FAULT_R_ODL);
	gpio_enable_interrupt(GPIO_USB_A1_FAULT_R_ODL);
	gpio_enable_interrupt(GPIO_USB_A0_FAULT_R_ODL);

	/* Enable BJ insertion interrupt */
	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_interrupt_init, HOOK_PRIO_INIT_I2C + 1);

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

/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
static int fsusb42umx_set_mux(const struct usb_mux*, mux_state_t, bool *);
const struct usb_mux_driver usbc_sbu_mux_driver = {
	.set = fsusb42umx_set_mux,
};

/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux usbc0_sbu_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &usbc_sbu_mux_driver,
};

const struct usb_mux usbc1_sbu_mux = {
	.usb_port = USBC_PORT_C1,
	.driver = &usbc_sbu_mux_driver,
};

const struct usb_mux usb_muxes[] = {
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
		.next_mux = &usbc1_sbu_mux,
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

struct ioexpander_config_t ioex_config[] = {
	[USBC_PORT_C0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_IO_EXPANDER_PORT_COUNT == USBC_PORT_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 0,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
	},
	[PWM_CH_LED1] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_LED2] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 4500,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/*
 * USB C0/C1 port SBU mux use standalone FSUSB42UMX
 * chip and it needs a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			      bool *ack_required)
{
	bool inverted = mux_state & USB_PD_MUX_POLARITY_INVERTED;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (me->usb_port == USBC_PORT_C0)
		RETURN_ERROR(ioex_set_level(IOEX_USB_C0_SBU_FLIP, inverted));
	else if (me->usb_port == USBC_PORT_C1)
		RETURN_ERROR(ioex_set_level(IOEX_USB_C1_SBU_FLIP, inverted));

	return EC_SUCCESS;
}

#define BJ_DEBOUNCE_MS		1000  /* Debounce time for BJ plug/unplug */

static int8_t bj_connected = -1;

static void bj_connect_deferred(void)
{
	struct charge_port_info pi = { 0 };
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_L);

	/* Debounce */
	if (connected == bj_connected)
		return;

	if (connected)
		board_get_bj_power(&pi.voltage, &pi.current);

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &pi);
	bj_connected = connected;
}
DECLARE_DEFERRED(bj_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void baseboard_bj_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&bj_connect_deferred_data, BJ_DEBOUNCE_MS * MSEC);
}

static void charge_port_init(void)
{
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* Report charge state from the barrel jack. */
	bj_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, charge_port_init, HOOK_PRIO_CHARGE_MANAGER_INIT + 1);

int board_set_active_charge_port(int port)
{
	int rv, i;

	CPRINTSUSB("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_active, bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_active = !gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L);
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_active != bj_requested)
			return EC_ERROR_INVAL;
	}

	/* Make sure BJ adapter is sourcing power */
	if (port == CHARGE_PORT_BARRELJACK &&
					gpio_get_level(GPIO_BJ_ADP_PRESENT_L)) {
		CPRINTSUSB("BJ port selected, but not present!");
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charger p%d", port);

	/*
	 * Disable PPCs on all ports which aren't enabled.
	 *
	 * Note: this assumes that the CHARGE_PORT_ enum is ordered with the
	 * type-c ports first always.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		rv = ppc_vbus_sink_enable(i, 0);
		if (rv) {
			CPRINTSUSB("Failed to disable C%d sink path", i);
			return rv;
		}
	}

	switch (port) {
	case CHARGE_PORT_TYPEC0:
	case CHARGE_PORT_TYPEC1:
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
		rv = ppc_vbus_sink_enable(port, 1);
		if (rv) {
			CPRINTSUSB("Failed to enable sink path");
			return rv;
		}
		break;
	case CHARGE_PORT_BARRELJACK:
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

int board_is_i2c_port_powered(int port)
{
	if (port == I2C_PORT_THERMAL_AP)
		/* SOC thermal i2c bus is unpowered in S0i3/S3/S5/Z1 */
		return chipset_in_state(CHIPSET_STATE_ANY_OFF |
					CHIPSET_STATE_ANY_SUSPEND) ? 0 : 1;
	/* All other i2c ports are always powered when EC is powered */
	return 1;
}

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
int board_aoz1380_set_vbus_source_current_limit(int port,
						enum tcpc_rp_value rp)
{
	int rv;

	/* Use the TCPC to set the current limit */
	rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

/* Called when the charge manager has switched to a new port. */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* TODO: blink led if power insufficient */
}

int extpower_is_present(void)
{
	return 1;
}

void sbu_fault_interrupt(enum ioex_signal signal)
{
	int port = (signal == IOEX_USB_C0_SBU_FAULT_ODL) ? 0 : 1;

	pd_handle_overcurrent(port);
}

void hdmi_fault_interrupt(enum gpio_signal signal)
{
	/* TODO: Report HDMI fault */
}

void dp_fault_interrupt(enum gpio_signal signal)
{
	/* TODO: Report DP fault */
}

void ext_charger_interrupt(enum gpio_signal signal)
{
	/* TODO: Handle ext charger interrupt */
}

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

static void reset_pd_port(int port, enum gpio_signal reset_gpio_l,
			  int hold_delay, int post_delay)
{
	gpio_set_level(reset_gpio_l, 0);
	msleep(hold_delay);
	gpio_set_level(reset_gpio_l, 1);
	if (post_delay)
		msleep(post_delay);
}


void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_pd_port(USBC_PORT_C0, GPIO_USB_C0_TCPC_RST_L,
		      NCT38XX_RESET_HOLD_DELAY_MS,
		      NCT3807_RESET_POST_DELAY_MS);

	/* Reset TCPC1 */
	reset_pd_port(USBC_PORT_C1, GPIO_USB_C1_TCPC_RST_L,
		      NCT38XX_RESET_HOLD_DELAY_MS,
		      NCT3807_RESET_POST_DELAY_MS);
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
 * b/175324615: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PCH_PWRBTN_L.
 */
void board_pwrbtn_to_pch(int level)
{
	timestamp_t start;
	const uint32_t timeout_rsmrst_rise_us = 30 * MSEC;

	/* Add delay for G3 exit if asserting PWRBTN_L and RSMRST_L is low. */
	if (!level && !gpio_get_level(GPIO_PCH_RSMRST_L)) {
		start = get_time();
		do {
			usleep(200);
			if (gpio_get_level(GPIO_PCH_RSMRST_L))
				break;
		} while (time_since32(start) < timeout_rsmrst_rise_us);

		if (!gpio_get_level(GPIO_PCH_RSMRST_L))
			ccprints("Error pwrbtn: RSMRST_L still low");

		msleep(G3_TO_PWRBTN_DELAY_MS);
	}
	gpio_set_level(GPIO_PCH_PWRBTN_L, level);
}

static void baseboard_chipset_suspend(void)
{
	/* TODO: Handle baseboard chipset suspend */
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

static void baseboard_chipset_resume(void)
{
	/* Enable the DP redriver, which powers on in S0 */
	tdp142_set_ctlsel(TDP142_CTLSEL_ENABLED);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

static bool ocp_tracker[CONFIG_USB_PD_PORT_MAX_COUNT];

static void set_usb_fault_output(void)
{
	bool fault_present = false;
	int i;

	/*
	 * EC must OR all fault alerts and pass them to USB_FAULT_ODL, including
	 * overcurrents.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		if (ocp_tracker[i])
			fault_present = true;

	fault_present = fault_present ||
			!gpio_get_level(GPIO_USB_A4_FAULT_R_ODL) ||
			!gpio_get_level(GPIO_USB_A3_FAULT_R_ODL) ||
			!gpio_get_level(GPIO_USB_A2_FAULT_R_ODL) ||
			!gpio_get_level(GPIO_USB_A1_FAULT_R_ODL) ||
			!gpio_get_level(GPIO_USB_A0_FAULT_R_ODL);

	gpio_set_level(GPIO_USB_FAULT_ODL, !fault_present);
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USBC_PORT_C0:
	case USBC_PORT_C1:
		ocp_tracker[port] = is_overcurrented;
		set_usb_fault_output();
		break;

	default:
		break;
	}
}

void baseboard_usb_fault_alert(enum gpio_signal signal)
{
	set_usb_fault_output();
}

void baseboard_en_pwr_pcore_s0(enum gpio_signal signal)
{

	/* EC must AND signals PG_LPDDR4X_S3_OD and PG_GROUPC_S0_OD */
	gpio_set_level(GPIO_EN_PWR_PCORE_S0_R,
					gpio_get_level(GPIO_PG_DDR4_S3_OD) &&
					gpio_get_level(GPIO_PG_GROUPC_S0_OD));
}

void baseboard_en_pwr_s0(enum gpio_signal signal)
{

	/* EC must AND signals SLP_S3_L and PG_PWR_S5 */
	gpio_set_level(GPIO_EN_PWR_S0_R,
					gpio_get_level(GPIO_SLP_S3_L) &&
					gpio_get_level(GPIO_PG_PWR_S5));

	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}
