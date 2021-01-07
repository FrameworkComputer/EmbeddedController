/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush family-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery_fuel_gauge.h"
#include "chipset.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "driver/ppc/aoz1380.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/nct38xx.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "isl9241.h"
#include "nct38xx.h"
#include "pi3usb9201.h"
#include "power.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

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

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Temp Sensors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = baseboard_get_temp,
		.idx = TEMP_SENSOR_SOC,
	},
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = baseboard_get_temp,
		.idx = TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_MEMORY] = {
		.name = "Memory",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = baseboard_get_temp,
		.idx = TEMP_SENSOR_MEMORY,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = baseboard_get_temp,
		.idx = TEMP_SENSOR_CPU,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT] = {
	[TEMP_SENSOR_SOC] = {
		.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = C_TO_K(32),
		.temp_fan_max = C_TO_K(75),
	},
	[TEMP_SENSOR_CHARGER] = {
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
	[TEMP_SENSOR_MEMORY] = {
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
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
			[EC_TEMP_THRESH_HALT] = C_TO_K(92),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/*
 * Battery info for all Guybrush battery types. Note that the fields
 * start_charging_min/max and charging_min/max are not used for the charger.
 * The effective temperature limits are given by discharging_min/max_c.
 *
 * Fuel Gauge (FG) parameters which are used for determining if the battery
 * is connected, the appropriate ship mode (battery cutoff) command, and the
 * charge/discharge FETs status.
 *
 * Ship mode (battery cutoff) requires 2 writes to the appropriate smart battery
 * register. For some batteries, the charge/discharge FET bits are set when
 * charging/discharging is active, in other types, these bits set mean that
 * charging/discharging is disabled. Therefore, in addition to the mask for
 * these bits, a disconnect value must be specified. Note that for TI fuel
 * gauge, the charge/discharge FET status is found in Operation Status (0x54),
 * but a read of Manufacturer Access (0x00) will return the lower 16 bits of
 * Operation status which contains the FET status bits.
 *
 * The assumption for battery types supported is that the charge/discharge FET
 * status can be read with a sb_read() command and therefore, only the register
 * address, mask, and disconnect value need to be provided.
 */
const struct board_batt_params board_battery_info[] = {
	/* AP18F4M / LIS4163ACPC */
	[BATTERY_AP18F4M] = {
		.fuel_gauge = {
			.manuf_name = "Murata KT00404001",
			.ship_mode = {
				.reg_addr = 0x3A,
				.reg_data = { 0xC574, 0xC574 },
			},
			.fet = {
				.reg_addr = 0x0,
				.reg_mask = 0x2000,
				.disconnect_val = 0x2000,
			}
		},
		.batt_info = {
			.voltage_max          = 8700,
			.voltage_normal       = 7600,
			.voltage_min          = 5500,
			.precharge_current    = 256,
			.start_charging_min_c = 0,
			.start_charging_max_c = 50,
			.charging_min_c       = 0,
			.charging_max_c       = 60,
			.discharging_min_c    = -20,
			.discharging_max_c    = 75,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(board_battery_info) == BATTERY_TYPE_COUNT);
const enum battery_type DEFAULT_BATTERY_TYPE = BATTERY_AP18F4M;

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

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		/* TODO: FIll in FP6 USB Mux configuration */
	},
	[USBC_PORT_C1] = {
		/* TODO: Fill in dynamically */
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

struct ioexpander_config_t ioex_config[] = {
	[USBC_PORT_C0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_IO_EXPANDER_PORT_COUNT == USBC_PORT_COUNT);


int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			     port < CONFIG_USB_PD_PORT_MAX_COUNT);
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
		      NCT38XX_RESET_POST_DELAY_MS);

	/* Reset TCPC1 */
	reset_pd_port(USBC_PORT_C1, GPIO_USB_C1_TCPC_RST_L,
		      NCT38XX_RESET_HOLD_DELAY_MS,
		      NCT38XX_RESET_POST_DELAY_MS);
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

int baseboard_get_temp(int idx, int *temp_ptr)
{
	/* TODO */
	return 0;
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
	/* Add delay for G3 exit if asserting PWRBTN_L and S5_PGOOD is low. */
	if (!level && !gpio_get_level(GPIO_S5_PGOOD)) {
		/*
		 * From measurement, wait 80 ms for RSMRST_L to rise after
		 * S5_PGOOD.
		 */
		msleep(G3_TO_PWRBTN_DELAY_MS);

		if (!gpio_get_level(GPIO_S5_PGOOD))
			ccprints("Error: pwrbtn S5_PGOOD low");
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
}

static void baseboard_chipset_suspend(void)
{
	/* Disable display and keyboard backlights. */
	gpio_set_level(GPIO_EC_DISABLE_DISP_BL, 1);
	ioex_set_level(GPIO_EN_KB_BL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

static void baseboard_chipset_resume(void)
{
	/* Enable display and keyboard backlights. */
	gpio_set_level(GPIO_EC_DISABLE_DISP_BL, 0);
	ioex_set_level(GPIO_EN_KB_BL, 1);
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
