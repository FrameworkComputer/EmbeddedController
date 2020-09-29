/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "board/asurada/it5205_sbu.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_tcs3400.h"
#include "driver/bc12/mt6360.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "regulator.h"
#include "spi.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void bc12_interrupt(enum gpio_signal signal);
static void ppc_interrupt(enum gpio_signal signal);
static void x_ec_interrupt(enum gpio_signal signal);

#include "gpio_list.h"

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

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
		.pcfsr_sel = PWM_PRESCALER_C4
	},
	[PWM_CH_LED2] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
	[PWM_CH_LED3] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Wake-up pins for hibernate */
enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

__override void board_hibernate_late(void)
{
	/*
	 * Turn off PP5000_A. Required for devices without Z-state.
	 * Don't care for devices with Z-state.
	 */
	gpio_set_level(GPIO_EN_PP5000_A, 0);

	/*
	 * GPIO_EN_SLP_Z not implemented in rev0/1,
	 * fallback to usual hibernate process.
	 */
	if (board_get_version() <= 1)
		return;

	isl9238c_hibernate(CHARGER_SOLO);

	gpio_set_level(GPIO_EN_SLP_Z, 1);

	/* should not reach here */
	__builtin_unreachable();
}

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PMIC_EC_PWRGD, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD"},
	{GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L"},
	{GPIO_AP_EC_WATCHDOG_L,
	  POWER_SIGNAL_ACTIVE_LOW | POWER_SIGNAL_DISABLE_AT_BOOT,
	  "AP_WDT_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Detect subboard */
static enum board_sub_board board_get_sub_board(void);

/* Initialize board. */
static void board_init(void)
{
	/* For Rev0 only. Set GPM0~6 1.8V input. */
	IT83XX_GPIO_GCR30 |= BIT(4);

	gpio_enable_interrupt(GPIO_AC_PRESENT);

	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);

	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
	gpio_enable_interrupt(GPIO_LID_ACCEL_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_tcpc_init(void)
{
	gpio_enable_interrupt(GPIO_USB_C0_PPC_INT_ODL);
	/* C1: GPIO_USB_C1_PPC_INT_ODL & HDMI: GPIO_PS185_EC_DP_HPD */
	gpio_enable_interrupt(GPIO_X_EC_GPIO2);

	/* If this is not a Type-C subboard, disable the task. */
	if (board_get_sub_board() != SUB_BOARD_TYPEC)
		task_disable_task(TASK_ID_PD_C1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"VBUS", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0, CHIP_ADC_CH0},
	{"BOARD_ID_0", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH1},
	{"BOARD_ID_1", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH2},
	{"CHARGER_AMON_R", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH3},
	{"CHARGER_PMON", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH6},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* BC12 */
const struct mt6360_config_t mt6360_config = {
	.i2c_port = 0,
	.i2c_addr_flags = MT6360_PMU_SLAVE_ADDR_FLAGS,
};

const struct pi3usb9201_config_t
		pi3usb9201_bc12_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	/* [0]: unused */
	[1] = {
		.i2c_port = 4,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	}
};

struct bc12_config bc12_ports[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{ .drv = &mt6360_drv },
	{ .drv = &pi3usb9201_drv },
};

static void bc12_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_BC12_INT_ODL)
		task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
	else
		task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}

static void board_sub_bc12_init(void)
{
	if (board_get_sub_board() == SUB_BOARD_TYPEC)
		gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);
	else
		/* If this is not a Type-C subboard, disable the task. */
		task_disable_task(TASK_ID_USB_CHG_P1);
}
/* Must be done after I2C and subboard */
DECLARE_HOOK(HOOK_INIT, board_sub_bc12_init, HOOK_PRIO_INIT_I2C + 1);

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
	{"bat_chg",  IT83XX_I2C_CH_A, 100, GPIO_I2C_A_SCL, GPIO_I2C_A_SDA},
	{"sensor",   IT83XX_I2C_CH_B, 100, GPIO_I2C_B_SCL, GPIO_I2C_B_SDA},
	{"usb0",     IT83XX_I2C_CH_C, 100, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
	{"usb1",     IT83XX_I2C_CH_E, 100, GPIO_I2C_E_SCL, GPIO_I2C_E_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY);
}

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
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv,
		.frs_en = GPIO_USB_C1_FRS_EN,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

static void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_USB_C0_PPC_INT_ODL)
		/* C0: PPC interrupt */
		syv682x_interrupt(0);
}

int debounced_hpd;

/**
 * Handle PS185 HPD changing state.
 */
static void ps185_hdmi_hpd_deferred(void)
{
	const int new_hpd = gpio_get_level(GPIO_PS185_EC_DP_HPD);

	/* HPD status not changed, probably a glitch, just return. */
	if (debounced_hpd == new_hpd)
		return;

	debounced_hpd = new_hpd;

	gpio_set_level(GPIO_EC_DPBRDG_HPD_ODL, !debounced_hpd);
	CPRINTS(debounced_hpd ? "HDMI plug" : "HDMI unplug");
}
DECLARE_DEFERRED(ps185_hdmi_hpd_deferred);

#define PS185_HPD_DEBOUCE 250

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ps185_hdmi_hpd_deferred_data, PS185_HPD_DEBOUCE);
}

/* HDMI/TYPE-C function shared subboard interrupt */
static void x_ec_interrupt(enum gpio_signal signal)
{
	int sub = board_get_sub_board();

	if (sub == SUB_BOARD_TYPEC)
		/* C1: PPC interrupt */
		syv682x_interrupt(1);
	else if (sub == SUB_BOARD_HDMI)
		hdmi_hpd_interrupt(signal);
	else
		CPRINTS("Undetected subboard interrupt.");
}

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_C0_PPC_INT_ODL) == 0;
	if (port == 1 && board_get_sub_board() == SUB_BOARD_TYPEC)
		return gpio_get_level(GPIO_USB_C1_PPC_INT_ODL) == 0;

	return 0;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO: check correct operation for Asurada */
}

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
	return 0;
}

void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: TCPC is embedded in the EC and processes interrupts in the
	 * chip code (it83xx/intc.c)
	 */
}

/* USB-A */
const int usb_port_enable[] = {
	GPIO_EN_PP5000_USB_A0_VBUS,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/* USB Mux */
static int board_ps8743_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int reg = 0;

	rv = ps8743_read(me, PS8743_REG_MODE, &reg);
	if (rv)
		return rv;

	/* Disable FLIP pin, enable I2C control. */
	reg |= PS8743_MODE_FLIP_REG_CONTROL;
	/* Disable CE_USB pin, enable I2C control. */
	reg |= PS8743_MODE_USB_REG_CONTROL;
	/* Disable CE_DP pin, enable I2C control. */
	reg |= PS8743_MODE_DP_REG_CONTROL;

	/*
	 * DP specific config
	 *
	 * Enable/Disable IN_HPD on the DB.
	 */
	gpio_set_level(GPIO_USB_C1_DP_IN_HPD,
		       mux_state & USB_PD_MUX_DP_ENABLED);

	return ps8743_write(me, PS8743_REG_MODE, reg);
}

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_MUX0,
		.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
		.driver = &it5205_usb_mux_driver,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_MUX1,
		.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
		.driver = &ps8743_usb_mux_driver,
		.board_set = &board_ps8743_mux_set,
	},
};

int board_set_active_charge_port(int port)
{
	int i;
	int is_valid_port = port == 0 || (port == 1 && board_get_sub_board() ==
							       SUB_BOARD_TYPEC);

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

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTF("Skip enable C%d", port);
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

	return EC_SUCCESS;
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

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_EC_BL_EN_OD, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_EC_BL_EN_OD, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Sub-board */

static enum board_sub_board board_get_sub_board(void)
{
	static enum board_sub_board sub = SUB_BOARD_NONE;

	if (sub != SUB_BOARD_NONE)
		return sub;

	/* HDMI board has external pull high. */
	if (gpio_get_level(GPIO_EC_X_GPIO3)) {
		sub = SUB_BOARD_HDMI;
		/* Only has 1 PPC with HDMI subboard */
		ppc_cnt = 1;
		/* EC_X_GPIO1 */
		gpio_set_flags(GPIO_EN_HDMI_PWR, GPIO_OUT_HIGH);
		/* X_EC_GPIO2 */
		gpio_set_flags(GPIO_PS185_EC_DP_HPD, GPIO_INT_BOTH);
		/* EC_X_GPIO3 */
		gpio_set_flags(GPIO_PS185_PWRDN_ODL, GPIO_ODR_HIGH);
	} else {
		sub = SUB_BOARD_TYPEC;
		/* EC_X_GPIO1 */
		gpio_set_flags(GPIO_USB_C1_FRS_EN, GPIO_OUT_LOW);
		/* X_EC_GPIO2 */
		gpio_set_flags(GPIO_USB_C1_PPC_INT_ODL,
			       GPIO_INT_BOTH | GPIO_PULL_UP);
		/* EC_X_GPIO3 */
		gpio_set_flags(GPIO_USB_C1_DP_IN_HPD, GPIO_OUT_LOW);
	}

	CPRINTS("Detect %s SUB", sub == SUB_BOARD_HDMI ? "HDMI" : "TYPEC");
	return sub;
}

static void sub_board_init(void)
{
	board_get_sub_board();
}
DECLARE_HOOK(HOOK_INIT, sub_board_init, HOOK_PRIO_INIT_I2C - 1);

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (board_get_sub_board() == SUB_BOARD_TYPEC)
		return CONFIG_USB_PD_PORT_MAX_COUNT;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
}

/* Lid */
#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif

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

/* Sensor */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct bmi_drv_data_t g_bmi160_data;
static struct stprivate_data g_lis2dwl_data;

/* Matrix to rotate accelrator into standard reference frame */
/* TODO: update the matrix after we have assembled unit */
static const mat33_fp_t mag_standard_ref = {
	{0, FLOAT_TO_FP(-1), 0},
	{FLOAT_TO_FP(-1), 0, 0},
	{0, 0, FLOAT_TO_FP(-1)},
};

/* TCS3400 private data */
static struct als_drv_data_t g_tcs3400_data = {
	.als_cal.scale = 1,
	.als_cal.uscale = 0,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kc */
		.cover_scale = ALS_CHANNEL_SCALE(1.0),     /* CT */
	},
};

#ifdef BOARD_ASURADA
/* Matrix to rotate accelerometer into standard reference frame */
/* for rev 0 */
static const mat33_fp_t base_standard_ref_rev0 = {
	{FLOAT_TO_FP(-1), 0, 0},
	{0, FLOAT_TO_FP(1), 0},
	{0, 0, FLOAT_TO_FP(-1)},
};

static void update_rotation_matrix(void)
{
	motion_sensors[BASE_ACCEL].rot_standard_ref =
		&base_standard_ref_rev0;
	motion_sensors[BASE_GYRO].rot_standard_ref =
		&base_standard_ref_rev0;
}
DECLARE_HOOK(HOOK_INIT, update_rotation_matrix, HOOK_PRIO_INIT_ADC + 1);
#endif

static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	/*
	 * TODO: calculate the actual coefficients and scaling factors
	 */
	.calibration.rgb_cal[X] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
	},
	.calibration.rgb_cal[Y] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.1),
	},
	.calibration.rgb_cal[Z] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
	},
	.calibration.irt = INT_TO_FP(1),
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
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
		.rot_standard_ref = NULL, /* identity matrix */
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			/* Sensor on for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
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
		.rot_standard_ref = NULL, /* identity matrix */
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
	[BASE_MAG] = {
		.name = "Base Mag",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_MAG,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = BIT(11), /* 16LSB / uT, fixed */
		.rot_standard_ref = &mag_standard_ref,
		.min_frequency = BMM150_MAG_MIN_FREQ,
		.max_frequency = BMM150_MAG_MAX_FREQ(SPECIAL),
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DWL,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dw12_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2dwl_data,
		.int_signal = GPIO_LID_ACCEL_INT_L,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
		.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
		.rot_standard_ref = NULL, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = LIS2DW12_ODR_MIN_VAL,
		.max_frequency = LIS2DW12_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &tcs3400_drv,
		.drv_data = &g_tcs3400_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = TCS3400_I2C_ADDR_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
		.min_frequency = TCS3400_LIGHT_MIN_FREQ,
		.max_frequency = TCS3400_LIGHT_MAX_FREQ,
		.config = {
			/* Run ALS sensor in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
			},
		},
	},
	[RGB_ALS] = {
		.name = "RGB Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT_RGB,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &tcs3400_rgb_drv,
		.drv_data = &g_tcs3400_rgb_data,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
		/* freq = 0 indicates we should not use sensor directly */
		.min_frequency = 0,
		.max_frequency = 0,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
