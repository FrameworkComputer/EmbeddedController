/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "pi3usb9201.h"
#include "power.h"
#include "power/qcom.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "shi_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Forward declaration */
static void tcpc_alert_event(enum gpio_signal signal);
static void usb0_evt(enum gpio_signal signal);
static void usb1_evt(enum gpio_signal signal);
static void usba_oc_interrupt(enum gpio_signal signal);
static void ppc_interrupt(enum gpio_signal signal);
static void board_connect_c0_sbu(enum gpio_signal s);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* GPIO Interrupt Handlers */
static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void usb0_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

static void usba_oc_deferred(void)
{
	/* Use next number after all USB-C ports to indicate the USB-A port */
	board_overcurrent_event(CONFIG_USB_PD_PORT_MAX_COUNT,
				!gpio_get_level(GPIO_USB_A0_OC_ODL));
}
DECLARE_DEFERRED(usba_oc_deferred);

static void usba_oc_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&usba_oc_deferred_data, 0);
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_SWCTL_INT_ODL:
		sn5s330_interrupt(0);
		break;
	case GPIO_USB_C1_SWCTL_INT_ODL:
		sn5s330_interrupt(1);
		break;
	default:
		break;
	}
}

static void board_connect_c0_sbu_deferred(void)
{
	/*
	 * If CCD_MODE_ODL asserts, it means there's a debug accessory connected
	 * and we should enable the SBU FETs.
	 */
	ppc_set_sbu(0, 1);
}
DECLARE_DEFERRED(board_connect_c0_sbu_deferred);

static void board_connect_c0_sbu(enum gpio_signal s)
{
	hook_call_deferred(&board_connect_c0_sbu_deferred_data, 0);
}

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Use 80 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	/*
	 * 1. launcher key mapped to (KSI_3, KSO_0):
	 *    change actual_key_mask[0] = 0x14 to 0x1c
	 * 2. T11 key not in keyboard (KSI_0,KSO_1):
	 *    change actual_key_mask[1] from 0xff to 0xfe
	 */
	.actual_key_mask = { 0x1c, 0xfe, 0xff, 0xff, 0xff, 0xf5, 0xff, 0xa4,
			     0xff, 0xfe, 0x55, 0xfa, 0xca },
	/* Other values should be the same as the default configuration. */
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
};

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C_POWER_SCL,
	  .sda = GPIO_EC_I2C_POWER_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_USB_C0_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C0_PD_SDA,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 1000,
	  .scl = GPIO_EC_I2C_USB_C1_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C1_PD_SDA,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "eeprom",
	  .port = I2C_PORT_EEPROM,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_EEPROM_SCL,
	  .sda = GPIO_EC_I2C_EEPROM_SDA },
	{ .name = "sensor",
	  .port = I2C_PORT_SENSOR,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_SENSOR_SCL,
	  .sda = GPIO_EC_I2C_SENSOR_SDA },
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Measure VBUS through a 1/10 voltage divider */
	[ADC_VBUS] = { "VBUS", NPCX_ADC_CH1, ADC_MAX_VOLT * 10,
		       ADC_READ_MAX + 1, 0 },
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = { "AMON_BMON", NPCX_ADC_CH2, ADC_MAX_VOLT * 1000 / 18,
			    ADC_READ_MAX + 1, 0 },
	/*
	 * ISL9238 PSYS output is 1.44 uA/W over 5.6K resistor, to read
	 * 0.8V @ 99 W, i.e. 124000 uW/mV. Using ADC_MAX_VOLT*124000 and
	 * ADC_READ_MAX+1 as multiplier/divider leads to overflows, so we
	 * only divide by 2 (enough to avoid precision issues).
	 */
	[ADC_PSYS] = { "PSYS", NPCX_ADC_CH3,
		       ADC_MAX_VOLT * 124000 * 2 / (ADC_READ_MAX + 1), 2, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	[PWM_CH_DISPLIGHT] = { .channel = 5, .flags = 0, .freq = 20000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Power Path Controller */
struct ppc_config_t ppc_chips[] = {
	{ .i2c_port = I2C_PORT_TCPC0,
	  .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	  .drv = &sn5s330_drv },
	{ .i2c_port = I2C_PORT_TCPC1,
	  .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
	  .drv = &sn5s330_drv },
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

/*
 * Port-0/1 USB mux driver.
 *
 * The USB mux is handled by TCPC chip and the HPD update is through a GPIO
 * to AP. But the TCPC chip is also needed to know the HPD status; otherwise,
 * the mux misbehaves.
 */
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

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

/* BC1.2 */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_POWER,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
	{
		.i2c_port = I2C_PORT_EEPROM,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};
/* Initialize board. */
static void board_init(void)
{
	/* Enable BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable USB-A overcurrent interrupt */
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);

	/* Enable interrupt for BMI160 sensor */
	gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);

	/*
	 * The H1 SBU line for CCD are behind PPC chip. The PPC internal FETs
	 * for SBU may be disconnected after DP alt mode is off. Should enable
	 * the CCD_MODE_ODL interrupt to make sure the SBU FETs are connected.
	 */
	gpio_enable_interrupt(GPIO_CCD_MODE_ODL);

	/* Set the backlight duty cycle to 0. AP will override it later. */
	pwm_set_duty(PWM_CH_DISPLIGHT, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late()) {
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		board_reset_pd_mcu();
	}

	/* Enable PPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_SWCTL_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_SWCTL_INT_ODL);

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
	/*
	 * Pazquel/pazquel360 share the same firmware ,only pazquel360 has
	 * volume keys. So disable volume keys for pazquel board
	 */
	if (!board_has_side_volume_buttons()) {
		button_disable_gpio(BUTTON_VOLUME_UP);
		button_disable_gpio(BUTTON_VOLUME_DOWN);
		gpio_set_flags(GPIO_VOLUME_DOWN_L, GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_VOLUME_UP_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

static void da9313_pvc_mode_ctrl(int enable)
{
	/*
	 * On enable, PVC operates in automatic frequency mode.
	 * On disable, PVC operates in fixed frequency mode.
	 */
	if (enable)
		i2c_update8(I2C_PORT_POWER, DA9313_I2C_ADDR_FLAGS,
			    DA9313_REG_PVC_CTRL, DA9313_PVC_CTRL_PVC_MODE,
			    MASK_SET);
	else
		i2c_update8(I2C_PORT_POWER, DA9313_I2C_ADDR_FLAGS,
			    DA9313_REG_PVC_CTRL, DA9313_PVC_CTRL_PVC_MODE,
			    MASK_CLR);
}

void da9313_init(void)
{
	/* PVC operates in fixed frequency mode in S0. */
	da9313_pvc_mode_ctrl(0);
}
DECLARE_HOOK(HOOK_INIT, da9313_init, HOOK_PRIO_DEFAULT + 1);

void board_hibernate(void)
{
	int i;

	/*
	 * Sensors are unpowered in hibernate. Apply PD to the
	 * interrupt lines such that they don't float.
	 */
	gpio_set_flags(GPIO_ACCEL_GYRO_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	gpio_set_flags(GPIO_LID_ACCEL_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);

	/*
	 * Enable the PPC power sink path before EC enters hibernate;
	 * otherwise, ACOK won't go High and can't wake EC up. Check the
	 * bug b/170324206 for details.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		ppc_vbus_sink_enable(i, 1);
}

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/*
	 * Turn off display backlight in S3. AP has its own control. The EC's
	 * and the AP's will be AND'ed together in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);

	/* PVC operates in automatic frequency mode in S3. */
	da9313_pvc_mode_ctrl(1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* PVC operates in fixed frequency mode in S0. */
	da9313_pvc_mode_ctrl(0);
	/* Turn on display and keyboard backlight in S0. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	if (pwm_get_duty(PWM_CH_DISPLIGHT))
		pwm_enable(PWM_CH_DISPLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on S3 -> S5 transition */
static void board_shutdown_complete(void)
{
	if (pwm_get_duty(PWM_CH_DISPLIGHT)) {
		pwm_set_duty(PWM_CH_DISPLIGHT, 0);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, board_shutdown_complete,
	     HOOK_PRIO_DEFAULT);

__override uint32_t board_get_sku_id(void)
{
	static int sku_id = -1;

	if (sku_id == -1) {
		int bits[3];

		bits[0] = gpio_get_ternary(GPIO_SKU_ID0);
		bits[1] = gpio_get_ternary(GPIO_SKU_ID1);
		bits[2] = gpio_get_ternary(GPIO_SKU_ID2);
		sku_id = binary_first_base3_from_bits(bits, ARRAY_SIZE(bits));
	}

	return (uint32_t)sku_id;
}

void board_set_switchcap_power(int enable)
{
	gpio_set_level(GPIO_SWITCHCAP_ON, enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_get_level(GPIO_SWITCHCAP_ON);
}

int board_is_switchcap_power_good(void)
{
	return gpio_get_level(GPIO_SWITCHCAP_PG);
}

void board_reset_pd_mcu(void)
{
	cprints(CC_USB, "Resetting TCPCs...");
	cflush();

	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	msleep(PS8XXX_RESET_DELAY_MS);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
	msleep(PS8805_FW_INIT_DELAY_MS);
}

void board_set_tcpc_power_mode(int port, int mode)
{
	/* Ignore the "mode" to turn the chip on.  We can only do a reset. */
	if (mode)
		return;

	board_reset_pd_mcu();
}

int board_vbus_sink_enable(int port, int enable)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_vbus_sink_enable(port, enable);
}

int board_is_sourcing_vbus(int port)
{
	/* Both ports are controlled by PPC SN5S330 */
	return ppc_is_sourcing_vbus(port);
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* TODO(b/120231371): Notify AP */
	CPRINTS("p%d: overcurrent!", port);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charging port");

		/* Disable all ports. */
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (board_vbus_sink_enable(i, 0))
				CPRINTS("Disabling p%d sink path failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New charge port: p%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == port)
			continue;

		if (board_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (board_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * Ignore lower charge ceiling on PD transition if our battery is
	 * critical, as we may brownout.
	 */
	if (supplier == CHARGE_SUPPLIER_PD && charge_ma < 1500 &&
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		CPRINTS("Using max ilim %d", max_ma);
		charge_ma = max_ma;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

/* Mutexes */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(1), 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

static struct kionix_accel_data g_kx022_data;

static const mat33_fp_t lid_standard_ref_kx022 = { { FLOAT_TO_FP(1), 0, 0 },
						   { 0, FLOAT_TO_FP(1), 0 },
						   { 0, 0, FLOAT_TO_FP(1) } };

static struct bmi_drv_data_t g_bmi_data;
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref_kx022,
		.default_range = 2, /* g */
		/* We only use 2g because its resolution is only 8-bits */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	/*
	 * Note: bmi232: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI323,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi3xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI3_ADDR_I2C_PRIM,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 12500 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI323,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi3xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI3_ADDR_I2C_PRIM,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_clamshell()) {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* The sensors are not stuffed; don't allow lines to float */
		gpio_set_flags(GPIO_ACCEL_GYRO_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_LID_ACCEL_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	} else {
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable interrupt for the base accel sensor */
		gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);
	}
}
DECLARE_HOOK(HOOK_INIT, board_update_sensor_config_from_sku,
	     HOOK_PRIO_INIT_I2C + 2);

static uint8_t sku_id;

enum board_model {
	PAZQUEL,
	PAZQUEL360,
	UNKNOWN,
};

static const char *const model_name[] = {
	"PAZQUEL",
	"PAZQUEL360",
	"UNKNOWN",
};

static enum board_model get_model(void)
{
	if (sku_id == 0 || sku_id == 1 || sku_id == 2 || sku_id == 3 ||
	    sku_id == 4 || sku_id == 5 || sku_id == 6)
		return PAZQUEL;
	if (sku_id >= 8)
		return PAZQUEL360;
	return UNKNOWN;
}

int board_is_clamshell(void)
{
	return get_model() == PAZQUEL;
}

/* Read SKU ID from GPIO and initialize variables for board variants */
static void sku_init(void)
{
	sku_id = system_get_sku_id();
	CPRINTS("SKU: %u (%s)", sku_id, model_name[get_model()]);
}
DECLARE_HOOK(HOOK_INIT, sku_init, HOOK_PRIO_INIT_I2C + 1);

int board_has_side_volume_buttons(void)
{
	return get_model() == PAZQUEL360;
}
__override int mkbp_support_volume_buttons(void)
{
	return board_has_side_volume_buttons();
}
