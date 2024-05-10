/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Poppy board-specific configuration */

#include "adc.h"
#include "als.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_opt3001.h"
#include "driver/baro_bmp280.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/anx74xx.h"
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

#define USB_PD_PORT_ANX74XX 0

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

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void anx74xx_cable_det_handler(void)
{
	int cable_det = gpio_get_level(GPIO_USB_C0_CABLE_DET);
	int reset_n = gpio_get_level(GPIO_USB_C0_PD_RST_L);

	/*
	 * A cable_det low->high transition was detected. If following the
	 * debounce time, cable_det is high, and reset_n is low, then ANX3429 is
	 * currently in standby mode and needs to be woken up. Set the
	 * TCPC_RESET event which will bring the ANX3429 out of standby
	 * mode. Setting this event is gated on reset_n being low because the
	 * ANX3429 will always set cable_det when transitioning to normal mode
	 * and if in normal mode, then there is no need to trigger a tcpc reset.
	 */
	if (cable_det && !reset_n)
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET);
}
DECLARE_DEFERRED(anx74xx_cable_det_handler);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2 msec */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}
#endif

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
#ifdef BOARD_LUX
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 12.4K resistor, to read
	 * 0.8V @ 45 W, i.e. 56250 uW/mV. Using ADC_MAX_VOLT*56250 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = { "PSYS", NPCX_ADC_CH3,
		       ADC_MAX_VOLT * 56250 * 2 / (ADC_READ_MAX + 1), 2, 0 },
#endif
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "tcpc",
	  .port = NPCX_I2C_PORT0_0,
	  .kbps = 400,
	  .scl = GPIO_I2C0_0_SCL,
	  .sda = GPIO_I2C0_0_SDA },
	{ .name = "als",
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
			.addr_flags = ANX74XX_I2C_ADDR1_FLAGS,
		},
		.drv = &anx74xx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_0,
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
				.driver = &anx74xx_tcpm_usb_mux_driver,
				.hpd_update = &anx74xx_tcpc_update_hpd_status,
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

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/**
 * Power on (or off) a single TCPC.
 * minimum on/off delays are included.
 *
 * @param port	Port number of TCPC.
 * @param mode	0: power off, 1: power on.
 */
void board_set_tcpc_power_mode(int port, int mode)
{
	if (port != USB_PD_PORT_ANX74XX)
		return;

	if (mode) {
		gpio_set_level(GPIO_USB_C0_TCPC_PWR, 1);
		crec_msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	} else {
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
		crec_msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);
		crec_msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	}
}

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);

	crec_msleep(MAX(1, ANX74XX_RST_L_PWR_L_DELAY_MS));
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
	/* Disable TCPC0 (anx3429) power */
	gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);

	crec_msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	board_set_tcpc_power_mode(USB_PD_PORT_ANX74XX, 1);
}

void board_tcpc_init(void)
{
	int reg;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		gpio_set_level(GPIO_PP3300_USB_PD, 1);
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		crec_msleep(10);
		board_reset_pd_mcu();
	}

	/*
	 * TODO: Remove when Poppy is updated with PS8751 A3.
	 *
	 * Force PS8751 A2 to wake from low power mode.
	 * If PS8751 remains in low power mode after sysjump,
	 * TCPM_INIT will fail due to not able to access PS8751.
	 *
	 * NOTE: PS8751 A3 will wake on any I2C access.
	 */
	i2c_read8(NPCX_I2C_PORT0_1, 0x08, 0xA0, &reg);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
#endif

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
	{ "Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM0 },
	{ "Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM1 },
	{ "DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM2 },
	{ "eMMC", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM3 },
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
	 * Bits 5:4 (00)  - Nominal output voltage: 0.850V
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
	 * Bits 5:4 (00)  - Nominal output voltage: 0.850V
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

	/* Disable power button shutdown timer. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x14, 0x00);
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

	/* Enable sensors power supply */
	gpio_set_level(GPIO_PP1800_DX_SENSOR, 1);
	gpio_set_level(GPIO_PP3300_DX_SENSOR, 1);

	/* Enable VBUS interrupt */
	if (system_get_board_version() == 0) {
		/*
		 * crosbug.com/p/61929: rev0 does not have VBUS detection,
		 * force detection on both ports.
		 */
		gpio_set_flags(GPIO_USB_C0_VBUS_WAKE_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_USB_C1_VBUS_WAKE_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);

		vbus0_evt(GPIO_USB_C0_VBUS_WAKE_L);
		vbus1_evt(GPIO_USB_C1_VBUS_WAKE_L);
	} else {
		gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
		gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);
	}

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/*
	 * Set unused GPIO_LED_YELLO_C0[_OLD] as INPUT | PULL_UP
	 * for better S0ix/S3 power
	 */
	if (system_get_board_version() >= 5)
		gpio_set_flags(GPIO_LED_YELLOW_C0_OLD,
			       GPIO_INPUT | GPIO_PULL_UP);
	else
		gpio_set_flags(GPIO_LED_YELLOW_C0, GPIO_INPUT | GPIO_PULL_UP);

#ifdef BOARD_SORAKA
	/*
	 * TODO(b/64503543): Add proper options(#ifdef ) for Non-LTE SKU
	 * Set unused LTE related pins as INPUT | PULL_UP
	 * for better S0ix/S3 power
	 */
	if (system_get_board_version() >= 4) {
		gpio_set_flags(GPIO_WLAN_PE_RST, GPIO_INPUT | GPIO_PULL_UP);
		gpio_set_flags(GPIO_PP3300_DX_LTE, GPIO_INPUT | GPIO_PULL_UP);
		gpio_set_flags(GPIO_LTE_GPS_OFF_L, GPIO_INPUT | GPIO_PULL_UP);
		gpio_set_flags(GPIO_LTE_BODY_SAR_L, GPIO_INPUT | GPIO_PULL_UP);
		gpio_set_flags(GPIO_LTE_WAKE_L, GPIO_INPUT | GPIO_PULL_UP);
		gpio_set_flags(GPIO_LTE_OFF_ODL, GPIO_INPUT | GPIO_PULL_UP);
	}
#endif

#ifndef BOARD_LUX
	/*
	 * see (b/111215677): setting the internal PU/PD of the unused pin
	 * GPIO10 affects the ball K10 when it is selected to CR_SIN.
	 * Disabing the WKINEN bit of GPIO10 insteading setting its PU/PD to
	 * bypass this issue.
	 */
	NPCX_WKINEN(MIWU_TABLE_1, MIWU_GROUP_2) &= 0xFE;
#endif

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
#ifdef BOARD_LUX
		/* Disable cross-power with base, charger task will reenable. */
		board_enable_base_power(0);
#endif
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
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/* Adjust ILIM according to measurements to eliminate overshoot. */
	charge_ma = (charge_ma - 500) * 31 / 32 + 472;
	/* 5V is significantly more accurate than other voltages. */
	if (charge_mv > 5000)
		charge_ma -= 52;

	charge_set_input_current_limit(charge_ma, charge_mv);
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
	uint8_t id4;

	if (ver != -1)
		return ver;

	ver = 0;

	/* First 3 strappings are binary. */
	if (gpio_get_level(GPIO_BOARD_VERSION1))
		ver |= 0x01;
	if (gpio_get_level(GPIO_BOARD_VERSION2))
		ver |= 0x02;
	if (gpio_get_level(GPIO_BOARD_VERSION3))
		ver |= 0x04;

	/*
	 * 4th bit is using tristate strapping, ternary encoding:
	 * Hi-Z (id4=2) => 0, (id4=0) => 1, (id4=1) => 2
	 */
	id4 = gpio_get_ternary(GPIO_BOARD_VERSION4);
	ver |= ((id4 + 1) % 3) * 0x08;

	CPRINTS("Board ID = %d", ver);

	return ver;
}

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

static struct bmi_drv_data_t g_bmi160_data;
static struct opt3001_drv_data_t g_opt3001_data = {
	.scale = 1,
	.uscale = 0,
	.offset = 0,
};

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t mag_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

#ifdef BOARD_SORAKA
const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
				      { FLOAT_TO_FP(1), 0, 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

/* For rev3 and older */
const mat33_fp_t lid_standard_ref_old = { { FLOAT_TO_FP(-1), 0, 0 },
					  { 0, FLOAT_TO_FP(-1), 0 },
					  { 0, 0, FLOAT_TO_FP(1) } };
#else
const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };
#endif

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},

	[LID_GYRO] = {
	 .name = "Lid Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},

	[LID_MAG] = {
	 .name = "Lid Mag",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = BIT(11), /* 16LSB / uT, fixed */
	 .rot_standard_ref = &mag_standard_ref,
	 .min_frequency = BMM150_MAG_MIN_FREQ,
/* TODO(b/253292373): Remove when clang is fixed. */
DISABLE_CLANG_WARNING("-Wshift-count-negative")
	 .max_frequency = BMM150_MAG_MAX_FREQ(SPECIAL),
ENABLE_CLANG_WARNING("-Wshift-count-negative")
	},
	[LID_ALS] = {
	 .name = "Light",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_OPT3001,
	 .type = MOTIONSENSE_TYPE_LIGHT,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &opt3001_drv,
	 .drv_data = &g_opt3001_data,
	 .port = I2C_PORT_ALS,
	 .i2c_spi_addr_flags = OPT3001_I2C_ADDR_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 0x10000, /* scale = 1; uscale = 0 */
	 .min_frequency = OPT3001_LIGHT_MIN_FREQ,
	 .max_frequency = OPT3001_LIGHT_MAX_FREQ,
	 .config = {
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 1000,
		},
	 },
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

#ifdef BOARD_SORAKA
static void board_sensor_init(void)
{
	/* Old soraka use a different reference matrix */
	if (system_get_board_version() <= 3) {
		motion_sensors[LID_ACCEL].rot_standard_ref =
			&lid_standard_ref_old;
		motion_sensors[LID_GYRO].rot_standard_ref =
			&lid_standard_ref_old;
	}
}
DECLARE_HOOK(HOOK_INIT, board_sensor_init, HOOK_PRIO_DEFAULT);
#endif

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

int board_has_working_reset_flags(void)
{
	int version = system_get_board_version();

	/* Boards Rev1 and Rev2 will lose reset flags on power cycle. */
	if ((version == 1) || (version == 2))
		return 0;

	/* All other board versions should have working reset flags */
	return 1;
}
