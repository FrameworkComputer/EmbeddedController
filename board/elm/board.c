/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Oak board configuration */

#include "adc.h"
#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/anx7688.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/tmp432.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Dispaly port hardware can connect to port 0, 1 or neither. */
#define PD_PORT_NONE -1

void pd_mcu_interrupt(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

void deferred_reset_pd_mcu(void);
DECLARE_DEFERRED(deferred_reset_pd_mcu);

void usb_evt(enum gpio_signal signal)
{
	if (!gpio_get_level(GPIO_BC12_WAKE_L))
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_SOC_POWER_GOOD, POWER_SIGNAL_ACTIVE_HIGH, "POWER_GOOD" },
	{ GPIO_SUSPEND_L, POWER_SIGNAL_ACTIVE_LOW, "SUSPEND#_ASSERTED" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/*
	 * PSYS_MONITOR(PA2): ADC_IN2, 1.44 uA/W on 6.05k Ohm
	 * output in mW
	 */
	[ADC_PSYS] = { "PSYS", 379415, 4096, 0, STM32_AIN(2) },
	/* AMON_BMON(PC0): ADC_IN10, output in uV */
	[ADC_AMON_BMON] = { "AMON_BMON", 183333, 4096, 0, STM32_AIN(10) },
	/* VDC_BOOSTIN_SENSE(PC1): ADC_IN11, output in mV */
	[ADC_VBUS] = { "VBUS", 33000, 4096, 0, STM32_AIN(11) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

int anx7688_passthru_allowed(const struct i2c_port_t *port,
			     const uint16_t addr_flags)
{
	uint16_t addr = I2C_STRIP_FLAGS(addr_flags);

	/* Allow access to 0x2c (TCPC) */
	if (addr == 0x2c)
		return 1;

	CPRINTF("Passthru rejected on %x", addr);

	return 0;
}

/* I2C ports */
const struct i2c_port_t i2c_ports[] = { { .name = "battery",
					  .port = I2C_PORT_BATTERY,
					  .kbps = 100,
					  .scl = GPIO_I2C0_SCL,
					  .sda = GPIO_I2C0_SDA },
					{ .name = "pd",
					  .port = I2C_PORT_PD_MCU,
					  .kbps = 1000,
					  .scl = GPIO_I2C1_SCL,
					  .sda = GPIO_I2C1_SDA,
					  .passthru_allowed =
						  anx7688_passthru_allowed } };

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_ACCEL_PORT, 2, GPIO_SPI2_NSS },
	{ CONFIG_SPI_ACCEL_PORT, 2, GPIO_SPI2_NSS_DB }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/* TCPC */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC,
			.addr_flags = CONFIG_TCPC_I2C_BASE_ADDR_FLAGS,
		},
		.drv = &anx7688_tcpm_drv,
	},
};

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
#ifdef CONFIG_TEMP_SENSOR_TMP432
	{ "TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_LOCAL },
	{ "TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_REMOTE1 },
	{ "TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_REMOTE2 },
#endif
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.driver = &anx7688_usb_mux_driver,
			},
	},
};

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/**
 * Reset PD MCU
 *   ANX7688 needs a reset pulse of 50ms after power enable.
 */
void deferred_reset_pd_mcu(void)
{
	uint8_t state = gpio_get_level(GPIO_USB_C0_PWR_EN_L) |
			(gpio_get_level(GPIO_USB_C0_RST) << 1);

	CPRINTS("%s %d", __func__, state);
	switch (state) {
	case 0:
		/*
		 * PWR_EN_L low, RST low
		 * start reset sequence by turning off power enable
		 * and wait for 1ms.
		 */
		gpio_set_level(GPIO_USB_C0_PWR_EN_L, 1);
		hook_call_deferred(&deferred_reset_pd_mcu_data, 1 * MSEC);
		break;
	case 1:
		/*
		 * PWR_EN_L high, RST low
		 * pull PD reset pin and wait for another 1ms
		 */
		gpio_set_level(GPIO_USB_C0_RST, 1);
		hook_call_deferred(&deferred_reset_pd_mcu_data, 1 * MSEC);
		/* on PD reset, trigger PD task to reset state */
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET);
		break;
	case 3:
		/*
		 * PWR_EN_L high, RST high
		 * enable power and wait for 10ms then pull RESET_N
		 */
		gpio_set_level(GPIO_USB_C0_PWR_EN_L, 0);
		hook_call_deferred(&deferred_reset_pd_mcu_data, 10 * MSEC);
		break;
	case 2:
		/*
		 * PWR_EN_L low, RST high
		 * leave reset state
		 */
		gpio_set_level(GPIO_USB_C0_RST, 0);
		break;
	}
}

static void board_power_on_pd_mcu(void)
{
	/* check if power is already on */
	if (!gpio_get_level(GPIO_USB_C0_PWR_EN_L))
		return;

	gpio_set_level(GPIO_USB_C0_EXTPWR_EN, 1);
	hook_call_deferred(&deferred_reset_pd_mcu_data, 1 * MSEC);
}

void board_reset_pd_mcu(void)
{
	/* enable port controller's cable detection before reset */
	anx7688_enable_cable_detection(0);

	/* wait for 10ms, then start port controller's reset sequence */
	hook_call_deferred(&deferred_reset_pd_mcu_data, 10 * MSEC);
}

static int command_pd_reset(int argc, const char **argv)
{
	board_reset_pd_mcu();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(resetpd, command_pd_reset, "", "Reset PD IC");

/**
 * There is a level shift for AC_OK & LID_OPEN signal between AP & EC,
 * disable it (drive high) when AP is off, otherwise enable it (drive low).
 */
static void board_extpower_buffer_to_soc(void)
{
	/* Drive high when AP is off (G3), else drive low */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L,
		       chipset_in_state(CHIPSET_STATE_HARD_OFF) ? 1 : 0);
}

/* Initialize board. */
static void board_init(void)
{
	/* Enable Level shift of AC_OK & LID_OPEN signals */
	board_extpower_buffer_to_soc();
	/* Enable rev1 testing GPIOs */
	gpio_set_level(GPIO_SYSTEM_POWER_H, 1);
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);

	/* Enable BC 1.2 */
	gpio_enable_interrupt(GPIO_BC12_CABLE_INT);

	/* Check if typeC is already connected, and do 7688 power on flow */
	board_power_on_pd_mcu();

	/* Update VBUS supplier */
	usb_charger_vbus_change(0, !gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));

	/* Remap SPI2 to DMA channels 6 and 7 (0011) */
	STM32_DMA_CSELR(STM32_DMAC_CH6) |= (3 << 20) | (3 << 24);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Set active charge port -- only one port can active at a time.
 *
 * @param charge_port    Charge port to enable.
 *
 * Return EC_SUCCESS if charge port is accepted and made active.
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/* check if we are source VBUS on the port */
	int source = gpio_get_level(GPIO_USB_C0_5V_EN);

	if (is_real_port && source) {
		CPRINTF("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable charging port */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 1);
	} else {
		/* Enable charging port */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 0);
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
void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	/* Limit input current 95% ratio on elm board for safety */
	charge_ma = (charge_ma * 95) / 100;
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

/**
 * Set AP reset.
 * AP_RESET_L (PC3, CPU_WARM_RESET_L) is connected to PMIC SYSRSTB
 */
void board_set_ap_reset(int asserted)
{
	/* Signal is active-low */
	CPRINTS("ap warm reset(%d)", asserted);
	gpio_set_level(GPIO_AP_RESET_L, !asserted);
}

#ifdef CONFIG_TEMP_SENSOR_TMP432
static void tmp432_set_power_deferred(void)
{
	/* Shut tmp432 down if not in S0 && no external power */
	if (!extpower_is_present() && !chipset_in_state(CHIPSET_STATE_ON)) {
		if (EC_SUCCESS != tmp432_set_power(TMP432_POWER_OFF))
			CPRINTS("ERROR: Can't shutdown TMP432.");
		return;
	}

	/*  else, turn it on. */
	if (EC_SUCCESS != tmp432_set_power(TMP432_POWER_ON))
		CPRINTS("ERROR: Can't turn on TMP432.");
}
DECLARE_DEFERRED(tmp432_set_power_deferred);
#endif

/**
 * Hook of AC change. turn on/off tmp432 depends on AP & AC status.
 */
static void board_extpower(void)
{
	board_extpower_buffer_to_soc();
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Called on AP S5 -> S3 transition, and before HOOK_CHIPSET_STARTUP */
static void board_chipset_pre_init(void)
{
	/* Enable level shift of AC_OK when power on */
	board_extpower_buffer_to_soc();

	/* Enable SPI for KX022 */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);

	/* Set all four SPI pins to high speed */
	/* pins D0/D1/D3/D4 */
	STM32_GPIO_OSPEEDR(GPIO_D) |= 0x000003cf;
	/* pins F6 */
	STM32_GPIO_OSPEEDR(GPIO_F) |= 0x00003000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(&spi_devices[0], 1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable level shift to SoC when shutting down */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L, 1);

	spi_enable(&spi_devices[0], 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	gpio_config_module(MODULE_SPI_CONTROLLER, 0);

	/*
	 * Calling gpio_config_module sets disabled alternate function pins to
	 * GPIO_INPUT.  But to prevent leakage we want to set GPIO_OUT_LOW
	 */
	gpio_set_flags_by_mask(GPIO_D, 0x1a, GPIO_OUT_LOW);
	gpio_set_level(GPIO_SPI2_NSS, 0);
	gpio_set_level(GPIO_SPI2_NSS_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Motion sensors */
/* Mutexes */
static struct mutex g_kx022_mutex[2];

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(1), 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

/* KX022 private data */
struct kionix_accel_data g_kx022_data[2];

struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &kionix_accel_drv,
		.mutex = &g_kx022_mutex[0],
		.drv_data = &g_kx022_data[0],
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(0),
		.rot_standard_ref = &base_standard_ref,
		.default_range = 2, /* g, enough for lid angle calculation. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},

	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_kx022_mutex[1],
		.drv_data = &g_kx022_data[1],
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(1),
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

__override void lid_angle_peripheral_enable(int enable)
{
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);

	/* enable/disable touchpad */
	gpio_set_level(GPIO_EN_TP_INT_L, !enable);
}

uint16_t tcpc_get_alert_status(void)
{
	return gpio_get_level(GPIO_PD_MCU_INT) ? PD_STATUS_TCPC_ALERT_0 : 0;
}
