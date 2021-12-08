/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cherry baseboard-specific configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charger.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_icm42607.h"
#include "driver/bc12/mt6360.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/rt1718s.h"
#include "driver/ppc/syv682x.h"
#include "driver/retimer/ps8802.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/anx3443.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power_button.h"
#include "power.h"
#include "pwm_chip.h"
#include "pwm.h"
#include "regulator.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usbc_ppc.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_tc_sm.h"

static void bc12_interrupt(enum gpio_signal signal);
static void ppc_interrupt(enum gpio_signal signal);
static void xhci_init_done_interrupt(enum gpio_signal signal);

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/* Wake-up pins for hibernate */
enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* Override default setting, called after charger_chips_init */
static void baseboard_charger_init(void)
{
	/* b/198707662#comment9 */
	int reg = (4096 / ISL9238_INPUT_VOLTAGE_REF_STEP)
			<< ISL9238_INPUT_VOLTAGE_REF_SHIFT;

	i2c_write16(I2C_PORT_CHARGER, ISL923X_ADDR_FLAGS,
			ISL9238_REG_INPUT_VOLTAGE, reg);
}
DECLARE_HOOK(HOOK_INIT, baseboard_charger_init, HOOK_PRIO_DEFAULT + 2);

__override void board_hibernate_late(void)
{
	/*
	 * Turn off PP5000_A. Required for devices without Z-state.
	 * Don't care for devices with Z-state.
	 */
	gpio_set_level(GPIO_EN_PP5000_A, 0);
	isl9238c_hibernate(CHARGER_SOLO);
	gpio_set_level(GPIO_EN_SLP_Z, 1);

	/* should not reach here */
	__builtin_unreachable();
}

static void board_tcpc_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_INT_ODL);
}
/* Must be done after I2C */
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

void rt1718s_tcpc_interrupt(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(1);
}

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"VBUS", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0, CHIP_ADC_CH0},
	{"BOARD_ID_0", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH1},
	{"BOARD_ID_1", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH2},
	/* AMON/BMON gain = 17.97 */
	{"CHARGER_AMON_R", ADC_MAX_MVOLT * 1000 / 17.97, ADC_READ_MAX + 1, 0,
	 CHIP_ADC_CH3},
	{"CHARGER_PMON", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH6},
	{"TEMP_SENSOR_CHG", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH7},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_CHARGER,
	},
};

/* PPC */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C0_FRS_EN,
	},
	{
		.i2c_port = I2C_PORT_PPC1,
		.i2c_addr_flags = RT1718S_I2C_ADDR1_FLAGS,
		.drv = &rt1718s_ppc_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* BC12 */
const struct mt6360_config_t mt6360_config = {
	.i2c_port = 0,
	.i2c_addr_flags = MT6360_PMU_I2C_ADDR_FLAGS,
};

__maybe_unused const struct pi3usb9201_config_t
		pi3usb9201_bc12_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0] = {
		.i2c_port = I2C_PORT_USB0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	}
	/* [1]: unused */
};

struct bc12_config bc12_ports[CONFIG_USB_PD_PORT_MAX_COUNT] = {
#ifdef CONFIG_BC12_DETECT_PI3USB9201
	{ .drv = &pi3usb9201_drv },
#elif defined(CONFIG_BC12_DETECT_MT6360)
	{ .drv = &mt6360_drv },
#else
#error must pick one of PI3USB9201 or MT6360 for port 0
#endif
	{ .drv = &rt1718s_bc12_drv },
};

static void bc12_interrupt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
}

static void ppc_interrupt(enum gpio_signal signal)
{
	syv682x_interrupt(0);
}

/* PWM */

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED1] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED2] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED3] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = 0,
		.freq_hz = 10000, /* SYV226 supports 10~100kHz */
		.pcfsr_sel = PWM_PRESCALER_C6,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_EC_BL_EN_OD, 1);
	gpio_set_level(GPIO_DP_DEMUX_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_EC_BL_EN_OD, 0);
	gpio_set_level(GPIO_DP_DEMUX_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* USB-A */
const int usb_port_enable[] = {
	GPIO_EN_PP5000_USB_A0_VBUS_X,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

__maybe_unused void xhci_init_done_interrupt(enum gpio_signal signal)
{
	enum usb_charge_mode mode = gpio_get_level(signal) ?
		USB_CHARGE_MODE_ENABLED : USB_CHARGE_MODE_DISABLED;

	for (int i = 0; i < USB_PORT_COUNT; i++)
		usb_charge_set_mode(i, mode, USB_ALLOW_SUSPEND_CHARGE);

	/*
	 * Trigger hard reset to cycle Vbus on Type-C ports, recommended by
	 * USB 3.2 spec 10.3.1.1.
	 */
	if (gpio_get_level(signal)) {
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			if (tc_is_attached_src(i))
				pd_dpm_request(i, DPM_REQUEST_HARD_RESET_SEND);
		}
	}
}

/* USB Mux */

const struct usb_mux usbc0_virtual_mux = {
	.usb_port = 0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

const struct usb_mux usbc1_virtual_mux = {
	.usb_port = 1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};

static int board_ps8762_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	/* Make sure the PS8802 is awake */
	RETURN_ERROR(ps8802_i2c_wake(me));

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		RETURN_ERROR(ps8802_i2c_field_update16(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_USB_SSEQ_LEVEL,
					PS8802_USBEQ_LEVEL_UP_MASK,
					PS8802_USBEQ_LEVEL_UP_12DB));
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		RETURN_ERROR(ps8802_i2c_field_update8(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_DPEQ_LEVEL,
					PS8802_DPEQ_LEVEL_UP_MASK,
					PS8802_DPEQ_LEVEL_UP_12DB));
	}

	return EC_SUCCESS;
}

static int board_ps8762_mux_init(const struct usb_mux *me)
{
	return ps8802_i2c_field_update8(
			me, PS8802_REG_PAGE1,
			PS8802_REG_DCIRX,
			PS8802_AUTO_DCI_MODE_DISABLE | PS8802_FORCE_DCI_MODE,
			PS8802_AUTO_DCI_MODE_DISABLE);
}

static int board_anx3443_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	gpio_set_level(GPIO_USB_C1_DP_IN_HPD,
		       mux_state & USB_PD_MUX_DP_ENABLED);
	return EC_SUCCESS;
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX0,
		.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS,
		.driver = &ps8802_usb_mux_driver,
		.next_mux = &usbc0_virtual_mux,
		.board_init = &board_ps8762_mux_init,
		.board_set = &board_ps8762_mux_set,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX1,
		.i2c_addr_flags = ANX3443_I2C_ADDR0_FLAGS,
		.driver = &anx3443_usb_mux_driver,
		.next_mux = &usbc1_virtual_mux,
		.board_set = &board_anx3443_mux_set,
	},
};

/*
 * I2C channels (A, B, and C) are using the same timing registers (00h~07h)
 * at default.
 * In order to set frequency independently for each channels,
 * We use timing registers 09h~0Bh, and the supported frequency will be:
 * 50KHz, 100KHz, 400KHz, or 1MHz.
 * I2C channels (D, E and F) can be set different frequency on different ports.
 * The I2C(D/E/F) frequency depend on the frequency of SMBus Module and
 * the individual prescale register.
 * The frequency of SMBus module is 24MHz on default.
 * The allowed range of I2C(D/E/F) frequency is as following setting.
 * SMBus Module Freq = PLL_CLOCK / ((IT83XX_ECPM_SCDCR2 & 0x0F) + 1)
 * (SMBus Module Freq / 510) <=  I2C Freq <= (SMBus Module Freq / 8)
 * Channel D has multi-function and can be used as UART interface.
 * Channel F is reserved for EC debug.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "bat_chg",
		.port = IT83XX_I2C_CH_A,
		.kbps = 100,
		.scl  = GPIO_I2C_A_SCL,
		.sda  = GPIO_I2C_A_SDA
	},
	{
		.name = "sensor",
		.port = IT83XX_I2C_CH_B,
		.kbps = 400,
		.scl  = GPIO_I2C_B_SCL,
		.sda  = GPIO_I2C_B_SDA
	},
	{
		.name = "usb0",
		.port = IT83XX_I2C_CH_C,
		.kbps = 400,
		.scl  = GPIO_I2C_C_SCL,
		.sda  = GPIO_I2C_C_SDA
	},
	{
		.name = "usb1",
		.port = IT83XX_I2C_CH_E,
		.kbps = 1000,
		.scl  = GPIO_I2C_E_SCL,
		.sda  = GPIO_I2C_E_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY);
}

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB1,
			.addr_flags = RT1718S_I2C_ADDR1_FLAGS,
		},
		.drv = &rt1718s_tcpm_drv,
	},
};

__override int board_rt1718s_init(int port)
{
	static bool gpio_initialized;

	if (!system_jumped_late() && !gpio_initialized) {
		/* set GPIO 1~3 as push pull, as output, output low. */
		rt1718s_gpio_set_flags(port, RT1718S_GPIO1, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO2, GPIO_OUT_LOW);
		rt1718s_gpio_set_flags(port, RT1718S_GPIO3, GPIO_OUT_LOW);
		gpio_initialized = true;
	}

	/* gpio 1/2 output high when receiving frx signal */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_GPIO1_VBUS_CTRL,
			RT1718S_GPIO1_VBUS_CTRL_FRS_RX_VBUS, 0xFF));
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_GPIO2_VBUS_CTRL,
			RT1718S_GPIO2_VBUS_CTRL_FRS_RX_VBUS, 0xFF));

	/* Turn on SBU switch */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT2_SBU_CTRL_01,
				RT1718S_RT2_SBU_CTRL_01_SBU_VIEN |
				RT1718S_RT2_SBU_CTRL_01_SBU2_SWEN |
				RT1718S_RT2_SBU_CTRL_01_SBU1_SWEN,
				0xFF));
	/* Trigger GPIO 1/2 change when FRS signal received */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_FRS_CTRL3,
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1,
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO2 |
			RT1718S_FRS_CTRL3_FRS_RX_WAIT_GPIO1));
	/* Set FRS signal detect time to 46.875us */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_FRS_CTRL1,
			RT1718S_FRS_CTRL1_FRSWAPRX_MASK,
			0xFF));

	return EC_SUCCESS;
}

const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t cc_parameter = {
		.rising_time = IT83XX_TX_PRE_DRIVING_TIME_1_UNIT,
		.falling_time = IT83XX_TX_PRE_DRIVING_TIME_2_UNIT,
	};

	if (port == USBPD_PORT_A)
		return &cc_parameter;
	return NULL;
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL))
		return PD_STATUS_TCPC_ALERT_1;
	return 0;
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0: The internal TCPC on ITE EC does not have a reset signal,
	 * but it will get reset when the EC gets reset.
	 */
	/* C1: Add code if TCPC chips need a reset */
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin and PPC vconn because polarity and PPC vconn
	 * should already be set correctly in the PPC driver via the pd
	 * state machine.
	 */
}

int board_set_active_charge_port(int port)
{
	int i;
	bool is_valid_port = (port == 0 || port == 1);

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTS("Disabling C%d as sink failed.", i);
		}
		rt1718s_gpio_set_level(1, GPIO_EN_USB_C1_VBUS_L, 1);

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	rt1718s_gpio_set_level(1, GPIO_EN_USB_C1_VBUS_L, !(port == 1));

	return EC_SUCCESS;
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;

	/* TODO: add rt1718s */
	return 0;
}
/* SD Card */
int board_regulator_get_info(uint32_t index, char *name,
			     uint16_t *num_voltages, uint16_t *voltages_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_get_info(id, name, num_voltages,
					 voltages_mv);
}

int board_regulator_enable(uint32_t index, uint8_t enable)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_enable(id, enable);
}

int board_regulator_is_enabled(uint32_t index, uint8_t *enabled)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_is_enabled(id, enabled);
}

int board_regulator_set_voltage(uint32_t index, uint32_t min_mv,
				uint32_t max_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_set_voltage(id, min_mv, max_mv);
}

int board_regulator_get_voltage(uint32_t index, uint32_t *voltage_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_get_voltage(id, voltage_mv);
}

static void baseboard_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
#ifndef BOARD_CHERRY
	gpio_enable_interrupt(GPIO_AP_XHCI_INIT_DONE);
#endif
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT - 1);

__override int board_pd_set_frs_enable(int port, int enable)
{
	if (port == 1)
		/*
		 * Use set_flags (implemented by a single i2c write) instead
		 * of set_level (= i2c_update) to save one read operation in
		 * FRS path.
		 */
		rt1718s_gpio_set_flags(port, GPIO_EN_USB_C1_FRS,
				enable ? GPIO_OUT_HIGH : GPIO_OUT_LOW);
	return EC_SUCCESS;
}

__override int board_get_vbus_voltage(int port)
{
	int voltage = 0;

	switch (port) {
	case 0:
		voltage = adc_read_channel(ADC_VBUS);
		break;
	case 1:
		rt1718s_get_adc(port, RT1718S_ADC_VBUS1, &voltage);
		break;
	default:
		return 0;
	}

	return voltage;
}
