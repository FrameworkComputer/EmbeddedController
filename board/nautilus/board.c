/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Poppy board-specific configuration */

#include "adc.h"
#include "battery_smart.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/baro_bmp280.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/temp_sensor/bd99992gw.h"
#include "espi.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "panic.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
	    !gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;
	else if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
		 !gpio_get_level(GPIO_USB_C1_PD_RST_L))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

/* Set PD discharge whenever VBUS detection is high (i.e. below threshold). */
static void vbus_discharge_handler(void)
{
	if (system_get_board_version() >= 2) {
		pd_set_vbus_discharge(0,
				      gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));
		pd_set_vbus_discharge(1,
				      gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L));
	}
}
DECLARE_DEFERRED(vbus_discharge_handler);

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);

	hook_call_deferred(&vbus_discharge_handler_data, 0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);

	hook_call_deferred(&vbus_discharge_handler_data, 0);
}

void usb0_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Hibernate wake configuration */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Base detection */
	[ADC_BASE_DET] = { "BASE_DET", NPCX_ADC_CH0, ADC_MAX_VOLT,
			   ADC_READ_MAX + 1, 0 },
	/* Vbus sensing (10x voltage divider). */
	[ADC_VBUS] = { "VBUS", NPCX_ADC_CH2, ADC_MAX_VOLT * 10,
		       ADC_READ_MAX + 1, 0 },
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = { "AMON_BMON", NPCX_ADC_CH1, ADC_MAX_VOLT * 1000 / 18,
			    ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "tcpc0",
	  .port = NPCX_I2C_PORT0_0,
	  .kbps = 400,
	  .scl = GPIO_I2C0_0_SCL,
	  .sda = GPIO_I2C0_0_SDA },
	{ .name = "tcpc1",
	  .port = NPCX_I2C_PORT0_1,
	  .kbps = 400,
	  .scl = GPIO_I2C0_1_SCL,
	  .sda = GPIO_I2C0_1_SDA },
	{ .name = "charger",
	  .port = NPCX_I2C_PORT1,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "pmic",
	  .port = NPCX_I2C_PORT2,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "accelgyro",
	  .port = NPCX_I2C_PORT3,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.driver = &tcpci_tcpm_usb_mux_driver,
				.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			},
	}
};

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_0,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
};

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	crec_msleep(1);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		board_reset_pd_mcu();
	}

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

const struct temp_sensor_t temp_sensors[] = {
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },

	/* These BD99992GW temp sensors are only readable in S0 */
	{ "Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM1 },
	{ "DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Check if PMIC fault registers indicate VR fault. If yes, print out fault
 * register info to console. Additionally, set panic reason so that the OS can
 * check for fault register info by looking at offset 0x14(PWRSTAT1) and
 * 0x15(PWRSTAT2) in cros ec panicinfo.
 */
static void board_report_pmic_fault(const char *str)
{
	int vrfault, pwrstat1 = 0, pwrstat2 = 0;
	uint32_t info;

	/* RESETIRQ1 -- Bit 4: VRFAULT */
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, &vrfault) !=
	    EC_SUCCESS)
		return;

	if (!(vrfault & BIT(4)))
		return;

	/* VRFAULT has occurred, print VRFAULT status bits. */

	/* PWRSTAT1 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, &pwrstat1);

	/* PWRSTAT2 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, &pwrstat2);

	CPRINTS("PMIC VRFAULT: %s", str);
	CPRINTS("PMIC VRFAULT: PWRSTAT1=0x%02x PWRSTAT2=0x%02x", pwrstat1,
		pwrstat2);

	/* Clear all faults -- Write 1 to clear. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, BIT(4));
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, pwrstat1);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, pwrstat2);

	/*
	 * Status of the fault registers can be checked in the OS by looking at
	 * offset 0x14(PWRSTAT1) and 0x15(PWRSTAT2) in cros ec panicinfo.
	 */
	info = ((pwrstat2 & 0xFF) << 8) | (pwrstat1 & 0xFF);
	panic_set_reason(PANIC_SW_PMIC_FAULT, info, 0);
}

static void board_pmic_disable_slp_s0_vr_decay(void)
{
	/*
	 * VCCIOCNT:
	 * Bit 6    (0)   - Disable decay of VCCIO on SLP_S0# assertion
	 * Bits 5:4 (11)  - Nominal output voltage: 0.850V
	 * Bits 3:2 (10)  - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10)  - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x30, 0x3a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x34, 0x2a);

	/*
	 * V100ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (01) - Nominal voltage 1.0V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x37, 0x1a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (00) - Disable low power mode on SLP_S0# assertion
	 * Bits 5:4 (11) - Nominal voltage 1.0V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x38, 0x3a);
}

static void board_pmic_enable_slp_s0_vr_decay(void)
{
	/*
	 * VCCIOCNT:
	 * Bit 6    (1)   - Enable decay of VCCIO on SLP_S0# assertion
	 * Bits 5:4 (11)  - Nominal output voltage: 0.850V
	 * Bits 3:2 (10)  - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10)  - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x30, 0x7a);

	/*
	 * V18ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (10) - Nominal voltage set to 1.8V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x34, 0x6a);

	/*
	 * V100ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (01) - Nominal voltage 1.0V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x37, 0x5a);

	/*
	 * V085ACNT:
	 * Bits 7:6 (01) - Enable low power mode on SLP_S0# assertion
	 * Bits 5:4 (11) - Nominal voltage 1.0V
	 * Bits 3:2 (10) - VR set to AUTO on SLP_S0# de-assertion
	 * Bits 1:0 (10) - VR set to AUTO operating mode
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x38, 0x7a);
}

__override void power_board_handle_host_sleep_event(enum host_sleep_event state)
{
	if (state == HOST_SLEEP_EVENT_S0IX_SUSPEND)
		board_pmic_enable_slp_s0_vr_decay();
	else if (state == HOST_SLEEP_EVENT_S0IX_RESUME)
		board_pmic_disable_slp_s0_vr_decay();
}

static void board_pmic_init(void)
{
	board_report_pmic_fault("SYSJUMP");

	if (system_jumped_late())
		return;

	/* DISCHGCNT3 - enable 100 ohm discharge on V1.00A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3e, 0x04);

	board_pmic_disable_slp_s0_vr_decay();

	/* VRMODECTRL - disable low-power mode for all rails */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3b, 0x1f);
}
DECLARE_DEFERRED(board_pmic_init);

/* Initialize board. */
static void board_init(void)
{
	/*
	 * This enables pull-down on F_DIO1 (SPI MISO), and F_DIO0 (SPI MOSI),
	 * whenever the EC is not doing SPI flash transactions. This avoids
	 * floating SPI buffer input (MISO), which causes power leakage (see
	 * b/64797021).
	 */
	NPCX_PUPD_EN1 |= BIT(NPCX_DEVPU1_F_SPI_PUD_EN);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Level of sensor's I2C and interrupt are 3.3V on proto board */
	if (system_get_board_version() < 2) {
		/* ACCELGYRO3_INT_L */
		gpio_set_flags(GPIO_ACCELGYRO3_INT_L, GPIO_INT_FALLING);
		/* I2C3_SCL / I2C3_SDA */
		gpio_set_flags(GPIO_I2C3_SCL, GPIO_INPUT);
		gpio_set_flags(GPIO_I2C3_SDA, GPIO_INPUT);
	}

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);

	/* Initialize PMIC */
	hook_call_deferred(&board_pmic_init_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/* check if we are source VBUS on the port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTF("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_L :
					     GPIO_USB_C1_CHARGE_L,
			       1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L,
			       0);
	}

	return EC_SUCCESS;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

void board_hibernate(void)
{
	CPRINTS("Triggering PMIC shutdown.");
	uart_flush_output();

	/* Trigger PMIC shutdown. */
	if (i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x49, 0x01)) {
		/*
		 * If we can't tell the PMIC to shutdown, instead reset
		 * and don't start the AP. Hopefully we'll be able to
		 * communicate with the PMIC next time.
		 */
		CPRINTS("PMIC i2c failed.");
		system_reset(SYSTEM_RESET_LEAVE_AP_OFF);
	}

	/* Await shutdown. */
	while (1)
		;
}

int board_get_version(void)
{
	static int ver = -1;
	uint8_t id3;

	if (ver != -1)
		return ver;

	ver = 0;

	/* First 2 strappings are binary. */
	if (gpio_get_level(GPIO_BOARD_VERSION1))
		ver |= 0x01;
	if (gpio_get_level(GPIO_BOARD_VERSION2))
		ver |= 0x02;

	/*
	 * The 3rd strapping pin is tristate.
	 * id3 = 2 if Hi-Z, id3 = 1 if high, and id3 = 0 if low.
	 */
	id3 = gpio_get_ternary(GPIO_BOARD_VERSION3);
	ver |= id3 * 0x04;

	CPRINTS("Board ID = %d", ver);

	return ver;
}

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

static struct bmi_drv_data_t g_bmi160_data;

/* BMA255 private data */
static struct accelgyro_saved_data_t g_bma255_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(1), 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMA255,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bma2x2_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bma255_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = BMA255_ACCEL_MIN_FREQ,
	 .max_frequency = BMA255_ACCEL_MAX_FREQ,
	 .default_range = 2, /* g, to support lid angle calculation. */
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Enable or disable input devices, based on chipset state and tablet mode */
__override void lid_angle_peripheral_enable(int enable)
{
	/* If the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard.
	 */
	if (tablet_get_mode() || chipset_in_state(CHIPSET_STATE_ANY_OFF))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}

static void board_chipset_reset(void)
{
	board_report_pmic_fault("CHIPSET RESET");
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, board_chipset_reset, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	/* Enable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 1);

	gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

	gpio_set_level(GPIO_PP1800_DX_SENSOR, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 0);

	gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);

	gpio_set_level(GPIO_PP1800_DX_SENSOR, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

int board_has_working_reset_flags(void)
{
	int version = system_get_board_version();

	/* Boards Rev1, Rev2 and Rev3 will lose reset flags on power cycle. */
	if ((version == 1) || (version == 2) || (version == 3))
		return 0;

	/* All other board versions should have working reset flags */
	return 1;
}

/*
 * I2C callbacks to ensure bus free time for battery I2C transactions is at
 * least 5ms.
 */
#define BATTERY_FREE_MIN_DELTA_US (5 * MSEC)
static timestamp_t battery_last_i2c_time;

static int is_battery_i2c(const int port, const uint16_t addr_flags)
{
	return (port == I2C_PORT_BATTERY) && (addr_flags == BATTERY_ADDR_FLAGS);
}

void i2c_start_xfer_notify(const int port, const uint16_t addr_flags)
{
	unsigned int time_delta_us;

	if (!is_battery_i2c(port, addr_flags))
		return;

	time_delta_us = time_since32(battery_last_i2c_time);
	if (time_delta_us >= BATTERY_FREE_MIN_DELTA_US)
		return;

	crec_usleep(BATTERY_FREE_MIN_DELTA_US - time_delta_us);
}

void i2c_end_xfer_notify(const int port, const uint16_t addr_flags)
{
	if (!is_battery_i2c(port, addr_flags))
		return;

	battery_last_i2c_time = get_time();
}
