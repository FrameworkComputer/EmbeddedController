/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "button.h"
#include "cros_board_info.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ioexpander/pcal6408.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp5.h"
#include "driver/usb_mux/ps8740.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* This I2C moved. Temporarily detect and support the V0 HW. */
int I2C_PORT_BATTERY = I2C_PORT_BATTERY_V1;

/* Interrupt handler varies with DB option. */
void (*c1_tcpc_config_interrupt)(enum gpio_signal signal) = tcpc_alert_event;

void c1_tcpc_interrupt(enum gpio_signal signal)
{
	c1_tcpc_config_interrupt(signal);
}

/* Interrupt for C1 PPC with USB-C DB, HPD with HDMI DB. */
void (*c1_ppc_config_interrupt)(enum gpio_signal signal) = ppc_interrupt;

void c1_ppc_interrupt(enum gpio_signal signal)
{
	c1_ppc_config_interrupt(signal);
}

static void hdmi_hpd_handler(void)
{
	/* Pass HPD through from DB OPT1 HDMI connector to AP's DP1. */
	int hpd = gpio_get_level(GPIO_USB_C1_PPC_INT_ODL);
	gpio_set_level(GPIO_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct stprivate_data g_lis2dwl_data;
static struct lsm6dsm_data g_lsm6dsm_data = LSM6DSM_DATA;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(-1), 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LIS2DWL,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lis2dw12_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_lis2dwl_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 2, /* g, enough for laptop. */
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

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	 .config = {
		 /* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
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
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_GYRO),
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 1000 | ROUND_UP_FLAG, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* These IO expander GPIOs vary with DB option. */
enum ioex_signal IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT1;
enum ioex_signal IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT1;

static void pcal6408_handler(void)
{
	pcal6408_ioex_event_handler(IOEX_HDMI_PCAL6408);
}
DECLARE_DEFERRED(pcal6408_handler);

void pcal6408_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pcal6408_handler_data, 0);
}

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_130_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

/*****************************************************************************
 * Board suspend / resume
 */

static void board_chipset_resume(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		ioex_set_level(IOEX_EN_PWR_HDMI_DB, 1);
		crec_msleep(PI3HDX1204_POWER_ON_DELAY_MS);
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
		ioex_set_level(IOEX_EN_PWR_HDMI_DB, 0);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static int board_ps8743_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return EC_SUCCESS;
}

/*****************************************************************************
 * USB-C
 */

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state,
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
	.set = fsusb42umx_set_mux,
};

/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux_chain usbc0_sbu_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &usbc0_sbu_mux_driver,
		},
};

struct usb_mux usbc1_ps8xxx_mux = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG,
	.driver = &ps8743_usb_mux_driver,
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
		.mux = &usbc1_ps8xxx_mux,
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
	uint32_t board_version = 0;

	if (cbi_get_board_version(&board_version) == EC_SUCCESS &&
	    board_version >= 2) {
		ccprints("PS8743 USB MUX");
		usbc1_ps8xxx_mux.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG;
		usbc1_ps8xxx_mux.driver = &ps8743_usb_mux_driver;
		usbc1_ps8xxx_mux.board_set = &board_ps8743_mux_set;
	} else {
		ccprints("PS8740 USB MUX");
		usbc1_ps8xxx_mux.i2c_addr_flags = PS8740_I2C_ADDR0_FLAG;
		usbc1_ps8xxx_mux.driver = &ps8740_usb_mux_driver;
		usbc1_ps8xxx_mux.board_set = NULL;
	}

	if (ec_config_get_usb_db() == DALBOZ_DB_D_OPT2_USBA_HDMI) {
		ccprints("DB OPT2 HDMI");
		ioex_config[IOEX_HDMI_PCAL6408].flags = 0;
		ioex_init(IOEX_HDMI_PCAL6408);
		IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT2;
		IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT2;
		usb_port_enable[USBA_PORT_A1] = IOEX_EN_USB_A1_5V_DB_OPT2;
		c1_tcpc_config_interrupt = pcal6408_interrupt;
		c1_ppc_config_interrupt = hdmi_hpd_interrupt;
	} else {
		ccprints("DB OPT1 USBC");
		ioex_config[IOEX_C1_NCT3807].flags = 0;
		ioex_init(IOEX_C1_NCT3807);
		IOEX_USB_A1_RETIMER_EN = IOEX_USB_A1_RETIMER_EN_OPT1;
		IOEX_USB_A1_CHARGE_EN_DB_L = IOEX_USB_A1_CHARGE_EN_DB_L_OPT1;
		usb_port_enable[USBA_PORT_A1] = IOEX_EN_USB_A1_5V_DB_OPT1;
		c1_tcpc_config_interrupt = tcpc_alert_event;
		c1_ppc_config_interrupt = ppc_interrupt;
	}

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

	if (ec_config_has_lid_angle_tablet_mode()) {
		/* Enable Gyro interrupts */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_set_mode(0, TABLET_TRIGGER_LID);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
/*
 * Use HOOK_PRIO_INIT_I2C + 2 to be after ioex_init().
 */
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
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
	[IOEX_HDMI_PCAL6408] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PCAL6408_I2C_ADDR0,
		.drv = &pcal6408_ioexpander_drv,
		.flags = IOEX_FLAGS_DEFAULT_INIT_DISABLED,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB_OPT1,
};

static void check_v0_battery(void)
{
	uint32_t board_version = 0;

	cbi_get_board_version(&board_version);

	if (board_version == 1)
		I2C_PORT_BATTERY = I2C_PORT_BATTERY_V0;
}
/*
 * Use HOOK_PRIO_INIT_I2C so we re-map before init_battery_type() and
 * charger_chips_init() want to talk to the battery.
 */
DECLARE_HOOK(HOOK_INIT, check_v0_battery, HOOK_PRIO_INIT_I2C);
