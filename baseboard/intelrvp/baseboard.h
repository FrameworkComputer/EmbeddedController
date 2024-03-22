/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP board-specific configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#include "compiler.h"
#include "stdbool.h"

#ifdef VARIANT_INTELRVP_EC_IT8320
#include "ite_ec.h"
#elif defined(VARIANT_INTELRVP_EC_MCHP)
#include "mchp_ec.h"
#elif defined(VARIANT_INTELRVP_EC_NPCX)
#include "npcx_ec.h"
#else
#error "Define EC chip variant"
#endif

/*
 * TODO: b/241322365 - Watchdog error are observed if LTO is enabled
 * hence disabled it. Enable LTO once the fix is found.
 */

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* EC console commands  */
#define CONFIG_CMD_CHARGER_DUMP
#define CONFIG_CMD_KEYBOARD
#define CONFIG_CMD_USB_PD_CABLE
#define CONFIG_CMD_USB_PD_PE

/* Host commands  */
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET
#define CONFIG_HOSTCMD_PD_CONTROL

/* Port80 display */
#define CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY

/* RVP ID read retry count */
#define RVP_VERSION_READ_RETRY_CNT 2

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_SENSE_RESISTOR 5
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_TRICKLE_CHARGING

/*
 * Don't allow the system to boot to S0 when the battery is low and unable to
 * communicate on locked systems (which haven't PD negotiated)
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15001

/* Keyboard */

#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2

/* UART */
#define CONFIG_LOW_POWER_IDLE

/* USB-A config */

/* Enable USB-PD REV 3.0 */
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PID 0x8086

/* USB PD config */
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY
/* Treat 2nd reset from H1 as Power-On */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* USB MUX */
#ifdef CONFIG_USB_MUX_VIRTUAL
#define CONFIG_HOSTCMD_LOCATE_CHIP
#endif
#define CONFIG_USBC_SS_MUX

/* SoC / PCH */
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S4_RESIDENCY
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* EC */
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_WP_ALWAYS

/* Tablet mode */
#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE

/* Verified boot */
#define CONFIG_CRC8
#define CONFIG_SHA256_UNROLLED
#define CONFIG_VBOOT_HASH

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Temperature sensor */
#ifdef CONFIG_TEMP_SENSOR
#define CONFIG_STEINHART_HART_3V0_22K6_47K_4050B
#define CONFIG_TEMP_SENSOR_POWER
#define GPIO_TEMP_SENSOR_POWER GPIO_EN_PP3300_A
#define CONFIG_THERMISTOR
#define CONFIG_THROTTLE_AP
#ifdef CONFIG_PECI
#define CONFIG_PECI_COMMON
#endif /* CONFIG_PECI */
#endif /* CONFIG_TEMP_SENSOR */

/* I2C ports */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

/* EC exclude modules */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "module_id.h"
#include "registers.h"

FORWARD_DECLARE_ENUM(tcpc_rp_value);

/* PWM channels */
enum pwm_channel { PWM_CH_FAN, PWM_CH_COUNT };

/* FAN channels */
enum fan_channel {
	FAN_CH_0,
	FAN_CH_COUNT,
};

/* ADC channels */
enum adc_channel {
	ADC_TEMP_SNS_AMBIENT,
	ADC_TEMP_SNS_DDR,
	ADC_TEMP_SNS_SKIN,
	ADC_TEMP_SNS_VR,
	ADC_CH_COUNT,
};

/* Temperature sensors */
enum temp_sensor_id {
	TEMP_SNS_AMBIENT,
	TEMP_SNS_BATTERY,
	TEMP_SNS_DDR,
#ifdef CONFIG_PECI
	TEMP_SNS_PECI,
#endif
	TEMP_SNS_SKIN,
	TEMP_SNS_VR,
	TEMP_SENSOR_COUNT,
};

/* TODO(b:132652892): Verify the below numbers. */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* Define typical operating power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_VOLTAGE_MV 20000
#define PD_MAX_CURRENT_MA ((PD_MAX_POWER_MW / PD_MAX_VOLTAGE_MV) * 1000)
#define DC_JACK_MAX_VOLTAGE_MV 19000

/* TCPC gpios */
struct tcpc_gpio_t {
	enum gpio_signal pin;
	uint8_t pin_pol;
};

/* VCONN gpios */
struct vconn_gpio_t {
	enum gpio_signal cc1_pin;
	enum gpio_signal cc2_pin;
	uint8_t pin_pol;
};

struct tcpc_gpio_config_t {
	/* VBUS interrput */
	struct tcpc_gpio_t vbus;
	/* Source enable */
	struct tcpc_gpio_t src;
	/* Sink enable */
	struct tcpc_gpio_t snk;
#if defined(CONFIG_USBC_VCONN) && defined(CHIP_FAMILY_IT83XX)
	/* Enable VCONN */
	struct vconn_gpio_t vconn;
#endif
	/* Enable source ILIM */
	struct tcpc_gpio_t src_ilim;
};
extern const struct tcpc_gpio_config_t tcpc_gpios[];

struct mecc_1_0_tcpc_aic_gpio_config_t {
	/* TCPC interrupt */
	enum gpio_signal tcpc_alert;
	/* PPC interrupt */
	enum gpio_signal ppc_alert;
	/* PPC interrupt handler */
	void (*ppc_intr_handler)(int port);
};
extern const struct mecc_1_0_tcpc_aic_gpio_config_t mecc_1_0_tcpc_aic_gpios[];

void board_charging_enable(int port, int enable);
void board_vbus_enable(int port, int enable);
void board_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp);
int ioexpander_read_intelrvp_version(int *port0, int *port1);
void board_dc_jack_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);
bool is_typec_port(int port);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
