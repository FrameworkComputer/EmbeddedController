/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush family-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "cros_board_info.h"
#include "base_fw_config.h"
#include "battery_fuel_gauge.h"
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
#include "driver/retimer/anx7491.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/temp_sensor/tmp112.h"
#include "driver/usb_mux/anx7451.h"
#include "driver/usb_mux/amd_fp6.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "isl9241.h"
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

static void reset_nct38xx_port(int port);

/* Wake Sources */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

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
		.scl = GPIO_EC_I2C_USB_A0_C0_SCL,
		.sda = GPIO_EC_I2C_USB_A0_C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A1_C1_SCL,
		.sda = GPIO_EC_I2C_USB_A1_C1_SDA,
	},
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BATT_SCL,
		.sda = GPIO_EC_I2C_BATT_SDA,
	},
	{
		.name = "usb_mux",
		.port = I2C_PORT_USB_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_MUX_SDA,
	},
	{
		.name = "charger",
		.port = I2C_PORT_CHARGER,
		.kbps = 400,
		.scl = GPIO_EC_I2C_POWER_SCL,
		.sda = GPIO_EC_I2C_POWER_SDA,
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
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
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
	[ADC_CORE_IMON1] = {
		.name = "CORE_I",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SOC_IMON2] = {
		.name = "SOC_I",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temp Sensors */
static int board_get_memory_temp(int, int *);

const struct tmp112_sensor_t tmp112_sensors[] = {
	{ I2C_PORT_SENSOR, TMP112_I2C_ADDR_FLAGS0 },
	{ I2C_PORT_SENSOR, TMP112_I2C_ADDR_FLAGS1 },
};
BUILD_ASSERT(ARRAY_SIZE(tmp112_sensors) == TMP112_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_soc_temp,
		.idx = TMP112_SOC,
	},
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_MEMORY] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_memory_temp,
		.idx = ADC_TEMP_SENSOR_MEMORY,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
	[TEMP_SENSOR_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = tmp112_get_val,
		.idx = TMP112_AMB,
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
		/* TODO: Setting fan off to 0 so it's allways on */
		.temp_fan_off = C_TO_K(0),
		.temp_fan_max = C_TO_K(70),
	},
	[TEMP_SENSOR_CHARGER] = {
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
	[TEMP_SENSOR_CPU] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
			[EC_TEMP_THRESH_HALT] = C_TO_K(105),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		/*
		 * CPU temp sensor fan thresholds are high because they are a
		 * backup for the SOC temp sensor fan thresholds.
		 */
		.temp_fan_off = C_TO_K(60),
		.temp_fan_max = C_TO_K(90),
	},
	/*
	 * Note: Leave ambient entries at 0, both as it does not represent a
	 * hotspot and as not all boards have this sensor
	 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

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

const int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_PP5000_USB_A0_VBUS,
	IOEX_EN_PP5000_USB_A1_VBUS_DB,
};

static void baseboard_interrupt_init(void)
{
	/* Enable Power Group interrupts. */
	gpio_enable_interrupt(GPIO_PG_GROUPC_S0_OD);
	gpio_enable_interrupt(GPIO_PG_LPDDR4X_S3_OD);

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
static int fsusb42umx_set_mux(const struct usb_mux*, mux_state_t);
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
	.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &board_c1_ps8818_mux_set,
};

__overridable int board_c1_anx7451_mux_set(const struct usb_mux *me,
					   mux_state_t mux_state)
{
	CPRINTSUSB("C1: ANX7451 mux using default tuning");
	return 0;
}

struct usb_mux usbc1_anx7451 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
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

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us
	 */
	.output_settle_us = 80,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 0,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_LED_CHRG] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_LED_FULL] = {
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
	.pgood_gpio = GPIO_S0_PGOOD,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 6500,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it needs a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);

	return EC_SUCCESS;
}

static void setup_mux(void)
{
	switch (board_get_usb_c1_mux()) {
	case USB_C1_MUX_PS8818:
		CPRINTSUSB("C1: Setting PS8818 mux");
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_ps8818;
		break;
	case USB_C1_MUX_ANX7451:
		CPRINTSUSB("C1: Setting ANX7451 mux");
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_anx7451;
		break;
	default:
		CPRINTSUSB("C1: Mux is unknown");
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			     port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int cur_port = charge_manager_get_active_charge_port();

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
				reset_nct38xx_port(cur_port);
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


	/* Check if the port is sourcing VBUS. */
	if (tcpm_get_src_ctrl(port)) {
		CPRINTSUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Disallow changing ports if we booted in dead battery mode and don't
	 * have sufficient power to withstand Vbus loss.  The NCT3807 may
	 * continue to keep EN_SNK low on the original port and allow a
	 * dangerous level of voltage to pass through to the initial charge
	 * port (see b/183660105)
	 *
	 * If we do have sufficient power, then reset the dead battery port and
	 * set up Type-C error recovery on its connection.
	 */
	if (cur_port != CHARGE_PORT_NONE &&
			port != cur_port &&
			nct38xx_get_boot_type(cur_port) ==
						NCT38XX_BOOT_DEAD_BATTERY) {
		if (pd_is_battery_capable()) {
			reset_nct38xx_port(cur_port);
			pd_set_error_recovery(cur_port);
		} else {
			CPRINTSUSB("Battery too low for charge port change");
			return EC_ERROR_INVAL;
		}
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

int board_is_i2c_port_powered(int port)
{
	switch (port) {
	case I2C_PORT_USB_MUX:
	case I2C_PORT_SENSOR:
		/* USB mux and sensor i2c bus is unpowered in Z1 */
		return chipset_in_state(CHIPSET_STATE_HARD_OFF) ? 0 : 1;
	case I2C_PORT_THERMAL_AP:
		/* SOC thermal i2c bus is unpowered in S0i3/S3/S5/Z1 */
		return chipset_in_state(CHIPSET_STATE_ANY_OFF |
					CHIPSET_STATE_ANY_SUSPEND) ? 0 : 1;
	default:
		return 1;
	}
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

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

void sbu_fault_interrupt(enum ioex_signal signal)
{
	int port = (signal == IOEX_USB_C0_SBU_FAULT_ODL) ? 0 : 1;

	pd_handle_overcurrent(port);
}

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
	enum gpio_signal reset_gpio_l;

	if (port == USBC_PORT_C0)
		reset_gpio_l = GPIO_USB_C0_TCPC_RST_L;
	else if (port == USBC_PORT_C1)
		reset_gpio_l = GPIO_USB_C1_TCPC_RST_L;
	else
		/* Invalid port: do nothing */
		return;

	gpio_set_level(reset_gpio_l, 0);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_set_level(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT38XX_RESET_POST_DELAY_MS != 0)
		msleep(NCT38XX_RESET_POST_DELAY_MS);
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

static int board_get_memory_temp(int idx, int *temp_k)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;
	return get_temp_3v3_30k9_47k_4050b(idx, temp_k);
}

/**
 * Return if VBUS is sagging too low
 */
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

void board_hibernate(void)
{
	int port;

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
	if (battery_sleep_fuel_gauge() != EC_SUCCESS)
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

/* PS8811 is just a type-A USB retimer, reusing mux structure for convience. */
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

/* ANX7491 is just a type-A USB retimer, reusing mux structure for convience. */
const struct usb_mux usba1_anx7491 = {
	.usb_port = USBA_PORT_A1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = ANX7491_I2C_ADDR0_FLAGS,
	.board_init = &baseboard_a1_anx7491_retimer_init,
};

void baseboard_a1_retimer_setup(void)
{
	struct usb_mux a1_retimer;
	switch (board_get_usb_a1_retimer()) {
	case USB_A1_RETIMER_ANX7491:
		a1_retimer = usba1_anx7491;
		break;
	case USB_A1_RETIMER_PS8811:
		a1_retimer = usba1_ps8811;
		break;
	default:
		CPRINTSUSB("A1: Unknown retimer!");
		return;
	}
	a1_retimer.board_init(&a1_retimer);
}
DECLARE_DEFERRED(baseboard_a1_retimer_setup);

static void baseboard_chipset_suspend(void)
{
	/* Disable display and keyboard backlights. */
	gpio_set_level(GPIO_EC_DISABLE_DISP_BL, 1);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

static void baseboard_chipset_resume(void)
{
	/* Enable display and keyboard backlights. */
	gpio_set_level(GPIO_EC_DISABLE_DISP_BL, 0);
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
	/* Some retimers take several ms to be ready, so defer setup call */
	hook_call_deferred(&baseboard_a1_retimer_setup_data, 20 * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USBC_PORT_C0:
	case USBC_PORT_C1:
		gpio_set_level(GPIO_USB_C0_C1_FAULT_ODL, !is_overcurrented);
		break;

	default:
		break;
	}
}

void baseboard_en_pwr_pcore_s0(enum gpio_signal signal)
{

	/* EC must AND signals PG_LPDDR4X_S3_OD and PG_GROUPC_S0_OD */
	gpio_set_level(GPIO_EN_PWR_PCORE_S0_R,
					gpio_get_level(GPIO_PG_LPDDR4X_S3_OD) &&
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
