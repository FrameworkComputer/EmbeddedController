/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * config.h - Top-level configuration Chrome EC
 *
 * All configuration settings (CONFIG_*) are defined in this file or in a
 * sub-configuration file (config_chip.h, board.h, etc.) included by this file.
 *
 * Note that this file is included by assembly (.S) files.  Any C-isms such as
 * struct definitions or enums in a sub-configuration file MUST be guarded with
 * #ifndef __ASSEMBLER__ to prevent those C-isms from being evaluated by the
 * assembler.
 */

#ifndef __CROS_EC_CONFIG_H
#define __CROS_EC_CONFIG_H

#ifdef INCLUDE_ENV_CONFIG
/*
 * When building for an EC target, pick up the .h file which allows to
 * keep track of changing make variables.
 */
#include "env_config.h"
#endif

/*
 * All config options are listed alphabetically and described here.
 *
 * If you add a new config option somewhere in the code, you must add a
 * default value here and describe what it does.
 *
 * To get a list current list, run this command:
 *    git grep " CONFIG_" | grep -o "CONFIG_[A-Za-z0-9_]\+" | sort | uniq
 *
 * Some options are #defined here to enable them by default.  Chips or boards
 * may override this by #undef'ing them in config_chip.h or board.h,
 * respectively.
 *
 * TODO(crosbug.com/p/23758): Describe all of these.  Also describe the
 * HAS_TASK_* macro and how/when it should be used vs. a config define.  And
 * BOARD_*, CHIP_*, and CHIP_FAMILY_*.
 */

/* Add support for sensor FIFO */
#undef CONFIG_ACCEL_FIFO

/* Define the size of the global fifo, must be a power of 2. */
#undef CONFIG_ACCEL_FIFO_SIZE

/* The amount of free entries that trigger an interrupt to the AP. */
#undef CONFIG_ACCEL_FIFO_THRES

/*
 * Sensors in this mask are in forced mode: they needed to be polled
 * at their data rate frequency.
 */
#undef CONFIG_ACCEL_FORCE_MODE_MASK

/* Enable accelerometer interrupts. */
#undef CONFIG_ACCEL_INTERRUPTS

/*
 * Support "spoof" mode for sensors.  This allows sensors to have their values
 * spoofed to any arbitrary value.  This is useful for testing.
 */
#define CONFIG_ACCEL_SPOOF_MODE

/* Specify type of accelerometers attached. */
#undef CONFIG_ACCEL_BMA255
#undef CONFIG_ACCEL_KXCJ9
#undef CONFIG_ACCEL_KX022
/*
 * lis2dh/lis2de/lng2dm have the same register interface but different
 * supported resolution. In normal mode, lis2dh works in 10-bit resolution,
 * but lis2de/lng2dm only support 8bit resolution.
 *
 * Use the define for your correct chip and the CONFIG_ACCEL_LIS2D_COMMON will
 * automatically get defined.
 */
#undef CONFIG_ACCEL_LIS2DE
#undef CONFIG_ACCEL_LIS2DH
#undef CONFIG_ACCEL_LIS2DS
#undef CONFIG_ACCEL_LNG2DM
#undef CONFIG_ACCEL_LIS2D_COMMON

/*
 * lis2dw12 and lis2dwl have almost the same register interface.
 * lis2dw12 supports 4 low power modes but lis2dwl only supports one. lis2dwl
 * only supports 12 bit resolution under low power mode. But lis2dw12 can
 * support 12 bit or 14 bit resolution at different low power modes. In order
 * to get 14 bit resolution, lis2dwl does not use low power mode and lis2dw12
 * only uses 3 of 4 low power modes.
 *
 * Use the define for your correct chip and the CONFIG_ACCEL_LIS2DW_COMMON will
 * automatically get defined.
 */
#undef CONFIG_ACCEL_LIS2DW12
#undef CONFIG_ACCEL_LIS2DWL
#undef CONFIG_ACCEL_LIS2DW_COMMON

/* lis2dw driver support fifo and interrupt, but letting lid accel sensor work
 * at polling mode is a common selection in current usage model. We need get a
 * option to be able to select interrupt or polling (foced mode).
 */
#undef CONFIG_ACCEL_LIS2DW_AS_BASE

#undef CONFIG_ACCELGYRO_BMI160
#undef CONFIG_ACCELGYRO_BMI260
#undef CONFIG_ACCELGYRO_ICM426XX
#undef CONFIG_ACCELGYRO_LSM6DS0
/* Use CONFIG_ACCELGYRO_LSM6DSM for LSM6DSL, LSM6DSM, and/or LSM6DS3 */
#undef CONFIG_ACCELGYRO_LSM6DSM
#undef CONFIG_ACCELGYRO_LSM6DSO

/*
 * Some chips have a portion of memory which will remain powered even
 * during a reset.  This is called Always-On, or AON memory, and
 * typically has a separate firmware to manage the memory.  These
 * values can be used to configure the RAM layout for Always-On.
 *
 * See chip/ish/ for an example implementation.
 */
#undef CONFIG_AON_PERSISTENT_BASE
#undef CONFIG_AON_PERSISTENT_SIZE
#undef CONFIG_AON_RAM_BASE
#undef CONFIG_AON_RAM_SIZE

/* Add sensorhub function for LSM6DSM, required if 2nd device attached. */
#undef CONFIG_SENSORHUB_LSM6DSM

/* Specify type of Magnetometer attached. */
#undef CONFIG_MAG_LIS2MDL
#undef CONFIG_MAG_BMM150

/* Presence of a Bosh Sensortec BMM150 magnetometer behind a BMIxxx. */
#undef CONFIG_MAG_BMI_BMM150

/* Presence of a Bosh Sensortec BMM150 magnetometer behind a LSM6DSM. */
#undef CONFIG_MAG_LSM6DSM_BMM150

/* Presence of a ST LIS2MDL magnetometer behind a BMIxxx. */
#undef CONFIG_MAG_BMI_LIS2MDL

/* Presence of a ST LIS2MDL magnetometer behind a LSM6DSM. */
#undef CONFIG_MAG_LSM6DSM_LIS2MDL

/* Specify barometer attached */
#undef CONFIG_BARO_BMP280

/* When set, it indicates a secondary sensor is attached behind a BMIxxx. */
#undef CONFIG_BMI_SEC_I2C

/* When set, it indicates a secondary sensor is attached behind a LSM6DSM/L. */
#undef CONFIG_LSM6DSM_SEC_I2C

/* Support for BMIxxx hardware orientation sensor */
#undef CONFIG_BMI_ORIENTATION_SENSOR

/* Support for KIONIX KX022 hardware orientation sensor */
#undef CONFIG_KX022_ORIENTATION_SENSOR

/* Define the i2c address of the sensor behind the main sensor, if present. */
#undef CONFIG_ACCELGYRO_SEC_ADDR_FLAGS

/*
 * Define if either CONFIG_BMI_ORIENTATION_SUPPORT or
 * CONFIG_KX022_ORIENTATION_SUPPORT is set.
 */
#undef CONFIG_ORIENTATION_SENSOR

/* Support the orientation gesture */
#undef CONFIG_GESTURE_ORIENTATION

/* Support the body_detection */
#undef CONFIG_BODY_DETECTION

/* Which sensor body_detection use */
#undef CONFIG_BODY_DETECTION_SENSOR

/* Support custom setting of body_detection */
#undef CONFIG_BODY_DETECTION_CUSTOM

/* The max number of sampling data for 1 second */
#undef CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE

/* The threshold of acceleration variance */
#undef CONFIG_BODY_DETECTION_VAR_THRESHOLD
#undef CONFIG_BODY_DETECTION_CONFIDENCE_DELTA

/* How much noise affect threshold of variance */
#undef CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR

/* The confidence limit of on_body/off_body */
#undef CONFIG_BODY_DETECTION_ON_BODY_CON
#undef CONFIG_BODY_DETECTION_OFF_BODY_CON

/* The threshold duration to change to off_body */
#undef CONFIG_BODY_DETECTION_STATIONARY_DURATION

/*
 * Use the old standard reference frame for accelerometers. The old
 * reference frame is:
 * Z-axis: perpendicular to keyboard, pointing up, such that if the device
 *  is sitting flat on a table, the accel reads +G.
 * X-axis: in the plane of the keyboard, pointing from the front lip to the
 *  hinge, such that if the device is oriented with the front lip touching
 *  the table and the hinge directly above, the accel reads +G.
 * Y-axis: in the plane of the keyboard, pointing to the right, such that
 *  if the device is on it's left side, the accel reads +G.
 *
 * Also, in the old reference frame, the lid accel matches the base accel
 * readings when lid is closed.
 */
#undef CONFIG_ACCEL_STD_REF_FRAME_OLD

/* Set when INT2 is an output */
#undef CONFIG_ACCELGYRO_BMI160_INT2_OUTPUT
#undef CONFIG_ACCELGYRO_BMI260_INT2_OUTPUT

/* Specify type of Gyrometers attached. */
#undef CONFIG_GYRO_L3GD20H

/*
 * If this is defined, motion_sense sends sensor events to the AP in the format
 * +-----------+
 * | Timestamp |
 * |  Payload  |
 * | Timestamp |
 * |  Payload  |
 * |    ...    |
 * +-----------+
 *
 * If this is not defined, the events will be sent in the format
 * +-----------+
 * |  Payload  |
 * |  Payload  |
 * |  Payload  |
 * |    ...    |
 * | Timestamp |
 * +-----------+
 *
 * The former format enables improved filtering of sensor event timestamps on
 * the AP, but comes with stricter jitter requirements.
 */
#define CONFIG_SENSOR_TIGHT_TIMESTAMPS

/* Sync event driver */
#undef CONFIG_SYNC

/*
 * How many sync events to buffer before motion_sense gets a chance to run.
 * This is similar to sensor side fifos.
 * Note: for vsync, anything above 2 is probably plenty.
 */
#define CONFIG_SYNC_QUEUE_SIZE 8

/* Simulate command for sync */
#undef CONFIG_SYNC_COMMAND

/*
 * Define the event to raise when the sync event happens.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */
#undef CONFIG_SYNC_INT_EVENT

/* Compile chip support for digital-to-analog converter */
#undef CONFIG_DAC

/* Compile chip support for analog-to-digital convertor */
#undef CONFIG_ADC

/*
 * ADC sample time selection. The value is chip-dependent.
 * TODO: Replace this with CONFIG_ADC_PROFILE entries.
 */
#undef CONFIG_ADC_SAMPLE_TIME

/* Support voltage comparator */
#undef CONFIG_ADC_VOLTAGE_COMPARATOR

/* Include the ADC analog watchdog feature in the ADC code */
#define CONFIG_ADC_WATCHDOG

/*
 * Chip-dependent ADC configuration - select one.
 * SINGLE - Sample all inputs once when requested.
 * FAST_CONTINUOUS - Sample all inputs continuously using DMA, with minimal
 *                   sample time.
 */
#define CONFIG_ADC_PROFILE_SINGLE
#undef CONFIG_ADC_PROFILE_FAST_CONTINUOUS

/* Support AES symmetric-key algorithm */
#undef CONFIG_AES

/* Support AES-GCM */
#undef CONFIG_AES_GCM

/*
 * Some ALS modules may be connected to the EC. We need the command, and
 * specific drivers for each module.
 */
#ifdef HAS_TASK_ALS
#define CONFIG_ALS
#else
#undef CONFIG_ALS
#endif
#undef CONFIG_ALS_AL3010
#undef CONFIG_ALS_BH1730
/*
 * If defined, BH1730 uses board specific lux calculation formula parameters.
 * If not defined, BH1730 uses default parameters to calculate lux.
 */
#undef CONFIG_ALS_BH1730_LUXTH_PARAMS
#undef CONFIG_ALS_ISL29035
#undef CONFIG_ALS_OPT3001
#undef CONFIG_ALS_CM32183
/* Define the exact model ID present on the board: SI1141 = 41, SI1142 = 42, */
#undef CONFIG_ALS_SI114X
/* Check if the device revision is supported */
#undef CONFIG_ALS_SI114X_CHECK_REVISION

/* Define to include the clear channel driver for the tcs3400 light sensor */
#undef CONFIG_ALS_TCS3400

/*
 * Define to use atime tables in anti-saturation algos in the tcs3400 driver.
 * Defining this for a board makes the anti-saturation algorithm much more
 * efficient, but requires the board to have it's lens cover scale and k_channel
 * scales to be determined.  Define this for a board once it's added its
 * cover_scale and k_channel scale factors.
 */
#undef CONFIG_TCS_USE_LUX_TABLE

/*
 * Define the event to raise when a sensor interrupt triggers.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */
#undef CONFIG_ACCELGYRO_BMI160_INT_EVENT
#undef CONFIG_ACCELGYRO_BMI260_INT_EVENT
#undef CONFIG_ACCELGYRO_ICM426XX_INT_EVENT
#undef CONFIG_ACCEL_LSM6DSM_INT_EVENT
#undef CONFIG_ACCEL_LSM6DSO_INT_EVENT
#undef CONFIG_ACCEL_LIS2DS_INT_EVENT
#undef CONFIG_ACCEL_LIS2DW12_INT_EVENT
#undef CONFIG_ALS_SI114X_INT_EVENT
#undef CONFIG_ALS_TCS3400_INT_EVENT

/*
 * Enable Si114x to operate in polling mode. This config is used in conjunction
 * with CONFIG_ALS_SI114X_INT_EVENT. When polling is enabled, the read is
 * initiated in the same manner as when interrupts are used, but the event which
 * triggers the irq_handler is generated by deferred call using a fixed delay.
 */
#undef CONFIG_ALS_SI114X_POLLING

/*
 * Enable tcs3400 to operate without interrupt pin. This config is used in
 * conjunction with CONFIG_ALS_TCS3400_INT_EVENT. When this option is enabled,
 * the read is initiated in the same manner as when interrupts are used, but the
 * event which triggers the irq_handler is generated by deferred call.
 */
#undef CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT

/* Define which ALS sensor is used for dimming the lightbar when dark */
#undef CONFIG_ALS_LIGHTBAR_DIMMING

/* Link against third_party/cryptoc. */
#undef CONFIG_LIBCRYPTOC

/* Support AP hang detection host command and state machine */
#undef CONFIG_AP_HANG_DETECT

/* Support AP Warm reset Interrupt. */
#undef CONFIG_AP_WARM_RESET_INTERRUPT

/*
 * Enable support for CPU caches behaving according to the ARMv7-M ISA.
 * (so far, only the Cortex-M7 has such caches)
 */
#undef CONFIG_ARMV7M_CACHE

/*
 * Defined if core/ code provides assembly optimized implementation of
 * multiply-accumulate operations (32-bit operands, 64-bit result), for the
 * cores that lack native instructions.
 */
#undef CONFIG_ASSEMBLY_MULA32

/* Support audio codec. */
#undef CONFIG_AUDIO_CODEC
/* Audio codec caps. */
#undef CONFIG_AUDIO_CODEC_CAP_WOV_AUDIO_SHM
#undef CONFIG_AUDIO_CODEC_CAP_WOV_LANG_SHM
/* Support audio codec on DMIC. */
#undef CONFIG_AUDIO_CODEC_DMIC
/* Support audio codec software gain on DMIC. */
#undef CONFIG_AUDIO_CODEC_DMIC_SOFTWARE_GAIN
#undef CONFIG_AUDIO_CODEC_DMIC_MAX_SOFTWARE_GAIN
/* Support audio codec on I2S RX. */
#undef CONFIG_AUDIO_CODEC_I2S_RX
/* Support audio codec on WoV. */
#undef CONFIG_AUDIO_CODEC_WOV
/* Audio codec buffers. */
#undef CONFIG_AUDIO_CODEC_WOV_AUDIO_BUF_LEN
#undef CONFIG_AUDIO_CODEC_WOV_AUDIO_BUF_TYPE
#undef CONFIG_AUDIO_CODEC_WOV_LANG_BUF_LEN
#undef CONFIG_AUDIO_CODEC_WOV_LANG_BUF_TYPE

/*
 * Support controlling the display backlight based on the state of the lid
 * switch.  The EC will disable the backlight when the lid is closed.
 *
 * The GPIO should be named GPIO_ENABLE_BACKLIGHT if active high, or
 * GPIO_ENABLE_BACKLIGHT_L if active low. See CONFIG_BACKLIGHT_LID_ACTIVE_LOW.
 */
#undef CONFIG_BACKLIGHT_LID

/*
 * The backlight GPIO pin is active low and named GPIO_BACKLIGHT_ENABLED_L
 */
#undef CONFIG_BACKLIGHT_LID_ACTIVE_LOW

/*
 * If defined, EC will enable the backlight signal only if this GPIO is
 * asserted AND the lid is open.  This supports passing the backlight-enable
 * signal from the AP through EC.
 */
#undef CONFIG_BACKLIGHT_REQ_GPIO

/* Support base32 text encoding */
#undef CONFIG_BASE32

/*****************************************************************************/
/* Battery config */

/*
 * Support battery management and interrogation.
 *
 * This is implied by CONFIG_BATTERY_<device> (below); if not enabled and
 * CONFIG_BATTERY_PRESENT_CUSTOM is also disabled, the board is assumed to not
 * have or support a battery.
 */
#undef CONFIG_BATTERY

/*
 * Compile battery-specific code.
 *
 * Note that some boards have their own unique battery constants / functions.
 * In this case, those are provided in board/(boardname)/battery.c, and none of
 * these are defined.
 * Defining one of these will automatically define CONFIG_BATTERY near the end
 * of this file. If you add a new config here, you'll need to update that
 * check.
 */
#undef CONFIG_BATTERY_BQ20Z453
#undef CONFIG_BATTERY_BQ27541
#undef CONFIG_BATTERY_BQ27621
#undef CONFIG_BATTERY_BQ4050
#undef CONFIG_BATTERY_MAX17055
#undef CONFIG_BATTERY_MM8013

/*
 * MAX17055 support alert on voltage, current, temperature, and state-of-charge.
 */
#undef CONFIG_BATTERY_MAX17055_ALERT

/*
 * Enable full model driver of MAX17055.
 *
 * It provides a better soc estimation. ocv_table needs to be supplied.
 */
#undef CONFIG_BATTERY_MAX17055_FULL_MODEL

/* Compile mock battery support; used by tests. */
#undef CONFIG_BATTERY_MOCK

/* Maximum time to wake a non-responsive battery, in second */
#define CONFIG_BATTERY_PRECHARGE_TIMEOUT 30

/*
 * If defined, the charger will check a board specific function for battery hw
 * presence as an additional condition to determine if power on is allowed for
 * factory override, where allowing booting of a bare board with no battery and
 * no power button press is required.
 */
#undef CONFIG_BATTERY_HW_PRESENT_CUSTOM

/*
 * battery_is_present() support.
 *
 * Choice of battery driver normally determines the implementation of
 * battery_is_present(); it is also possible to provide a board-specific
 * implementation or note its presence from a GPIO level.
 *
 * If CONFIG_BATTERY is not enabled, a stub implementation that always returns
 * "not present" is provided unless CONFIG_BATTERY_PRESENT_CUSTOM is enabled.
 *
 * These options are mutually exclusive.
 */
/* The board provides a custom battery_is_present() implementation. */
#undef CONFIG_BATTERY_PRESENT_CUSTOM
/* Battery is present if the GPIO named by this define reads logic-low. */
#undef CONFIG_BATTERY_PRESENT_GPIO

/*
 * Compile smart battery support
 *
 * For batteries which support this specification:
 * http://sbs-forum.org/specs/sbdat110.pdf)
 */
#undef CONFIG_BATTERY_SMART

/* Chemistry of the battery device */
#undef CONFIG_BATTERY_DEVICE_CHEMISTRY

/*
 * If defined, the board must supply fuel gauge and battery information for
 * each supported battery. This information is then used for battery cut off
 * and to check the charge/discharge FET status.
 */
#undef CONFIG_BATTERY_FUEL_GAUGE

/*
 * Critical battery shutdown timeout (seconds)
 *
 * If the battery is at extremely low charge (and discharging) or extremely
 * high temperature, the EC will notify the AP and start a timer with the
 * timeout defined here. If the critical condition is not corrected before
 * the timeout expires, the EC will shut down the AP (if the AP is not already
 * off) and then optionally hibernate or cut off battery.
 */
#define CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT 30

/* Perform a battery cut-off when we reach the battery critical level */
#undef CONFIG_BATTERY_CRITICAL_SHUTDOWN_CUT_OFF

/* If the battery is too hot or too cold, stop charging */
#undef CONFIG_BATTERY_CHECK_CHARGE_TEMP_LIMITS

/*
 * Support battery cut-off as host command and console command.
 *
 * Once defined, you have to implement a board_cut_off_battery() function
 * in board/???/battery.c file.
 */
#undef CONFIG_BATTERY_CUT_OFF

/*
 * The default delay is 1 second. Define this if a board prefers
 * different delay.
 */
#undef CONFIG_BATTERY_CUTOFF_DELAY_US

/*
 * The board-specific battery.c implements get and set functions to read and
 * write arbirary vendor-specific parameters stored in the battery.
 * See include/battery.h for prototypes.
 */
#undef CONFIG_BATTERY_VENDOR_PARAM

/*
 * TODO(crosbug.com/p/29467): allows charging of a dead battery that
 * requests nil for current and voltage. Remove this workaround when
 * possible.
 */
#undef CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD

/*
 * Check for battery in disconnect state (similar to cut-off state). If this
 * battery is found to be in disconnect state, take it out of this state by
 * force-applying a charge current. This option requires
 * battery_get_disconnect_state() to be defined.
 */
#undef CONFIG_BATTERY_REVIVE_DISCONNECT

/*
 * Specify the battery percentage at which the host is told it is full.
 * If this value is not specified the default is 97% set in battery.h.
 */
#undef CONFIG_BATTERY_LEVEL_NEAR_FULL

/*
 * Use an alternative method to store battery information: Instead of writing
 * directly to host memory mapped region, this keeps the battery information in
 * ec_response_battery_static/dynamic_info structures, that can then be fetched
 * using host commands, or via EC_ACPI_MEM_BATTERY_INDEX command, which tells
 * the EC to update the shared memory.
 *
 * This is required on dual-battery systems, and on on hostless bases with a
 * battery.
 */
#undef CONFIG_BATTERY_V2

/*
 * Some fuel gauges in battery take some time to return valid battery params
 * when wake form dead battery.
 * It need to do precharge to take valid battery params.
 */
#undef CONFIG_BATTERY_DEAD_UNTIL_VALUE

/*
 * Number of batteries, only matters when CONFIG_BATTERY_V2 is used.
 */
#undef CONFIG_BATTERY_COUNT

/*
 * Smart battery driver should measure the voltage cell imbalance in the battery
 * pack.  This requires a battery driver capable of the measurement.
 */
#undef CONFIG_BATTERY_MEASURE_IMBALANCE

/*
 * If remaining capacity is x% of full capacity, remaining capacity is set
 * equal to full capacity.
 *
 * Some batteries don't update full capacity timely or don't update it at all.
 * On such systems, compensation is required to guarantee remaining_capacity
 * will be equal to full_capacity eventually. This used to be done in ACPI.
 */
#define CONFIG_BATT_FULL_FACTOR			98
#define CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE	4

/*
 * Powerd's full_factor. The value comes from:
 *   src/platform2/power_manager/default_prefs/power_supply_full_factor
 */
#define CONFIG_BATT_HOST_FULL_FACTOR		97

/*
 * Smart battery pass-through host commands.
 */
#undef CONFIG_SB_PASSTHROUGH

/*
 * Customize the battery behavior.
 */
#undef CONFIG_BATT_CUSTOM_SETTING

/*
 * Expose some data when it is needed.
 * For example, battery disconnect state
 */
#undef CONFIG_CHARGE_STATE_DEBUG

/* Include support for Bluetooth LE */
#undef CONFIG_BLUETOOTH_LE

/* Include support for testing the radio for Bluetooth LE */
#undef CONFIG_BLUETOOTH_LE_RADIO_TEST

/* Include support for the HCI and link layers for Bluetooth LE */
#undef CONFIG_BLUETOOTH_LE_STACK

/* Include debugging support for the Bluetooth link layer */
#undef CONFIG_BLUETOOTH_LL_DEBUG

/* Include debugging support for Bluetooth HCI */
#undef CONFIG_BLUETOOTH_HCI_DEBUG

/* Boot header storage offset. */
#undef CONFIG_BOOT_HEADER_STORAGE_OFF

/* Size of boot header in storage. */
#undef CONFIG_BOOT_HEADER_STORAGE_SIZE

/*****************************************************************************/
/* Bootblock config */

/* Pack AP-FW bootblock in EC image. */
#undef CONFIG_BOOTBLOCK

/*****************************************************************************/

/* EC can choose power signal gpio by schematic version */
#undef CONFIG_POWER_SIGNAL_RUNTIME_CONFIG

/* EC has GPIOs to allow board to reset RTC */
#undef CONFIG_BOARD_HAS_RTC_RESET

/*
 * Call board_config_post_gpio_init() after GPIOs are initialized.  See
 * include/board_config.h for more information.
 */
#undef CONFIG_BOARD_POST_GPIO_INIT

/*
 * Call board_config_pre_init() before any inits are called.  See
 * include/board_config.h for more information.
 */
#undef CONFIG_BOARD_PRE_INIT

/*
 * EC has the notion of board version either through resistors or EEPROM.
 * The common CONFIG_BOARD_VERSION is defined automatically when one of the
 * specific options is used.
 */
#undef CONFIG_BOARD_VERSION
/* The board version comes from Cros Board Info within EEPROM. */
#undef CONFIG_BOARD_VERSION_CBI
/* The board version function is defined in board code. */
#undef CONFIG_BOARD_VERSION_CUSTOM
/*
 * The board version is encoded with 3 GPIO signals where GPIO_BOARD_VERSION1
 * is the LSB.
 */
#undef CONFIG_BOARD_VERSION_GPIO

/* EC responses to a board defined I2C slave address */
#undef CONFIG_BOARD_I2C_SLAVE_ADDR_FLAGS

/*
 * The board is unable to distinguish EC reset from power-on so it should treat
 * all resets as triggered by RESET_PIN even if it is a POWER_ON reset.
 */
#undef CONFIG_BOARD_FORCE_RESET_PIN

/*
 * For some boards on power-on, the EC is reset by the H1 after power-on,
 * so the EC sees 2 resets. This config enables the EC to save a flag
 * on the first power-up restart, and then wait for the second reset before
 * any other setup is done (such as GPIOs, timers, UART etc.)
 * On the second reset, the saved flag is used to detect the previous
 * power-on, and treat the second reset as a power-on instead of a reset.
 *
 * NOTE: Implemented only for npcx
 */
#undef CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Permanent LM4 boot configuration */
#undef CONFIG_BOOTCFG_VALUE

/*****************************************************************************/
/* Modify the default behavior to make system bringup easier. */
#undef CONFIG_BRINGUP

/*
 * Enable debug prints / asserts that may helpful for debugging board bring-up,
 * but probably shouldn't be enabled for production for performance reasons.
 */
#undef CONFIG_DEBUG_BRINGUP

/*****************************************************************************/

/*
 * Support for entering recovery mode using the volume buttons or a dedicated
 * recovery button.  Note that these are *buttons* and not keys in the keyboard
 * matrix.
 */
#undef CONFIG_BUTTON_TRIGGERED_RECOVERY

/*
 * Compile detachable base support
 *
 * Enabled on all boards that have a detachable base.
 */
#undef CONFIG_DETACHABLE_BASE

/*
 * Indicates there is a dedicated recovery button.  Note, that if there are
 * volume buttons, a dedicated recovery button is not needed.  This is intended
 * because if a board has volume buttons, they can do everything a dedicated
 * recovery button can do.
 * For various reasons, on some platforms there may be multiple recovery inputs.
 * See b/149967026.
 */
#undef CONFIG_DEDICATED_RECOVERY_BUTTON
#undef CONFIG_DEDICATED_RECOVERY_BUTTON_2

/*
 * The board has volume up and volume down buttons.  Note, these are *buttons*
 * and not keys in the keyboard matrix.
 */
#undef CONFIG_VOLUME_BUTTONS

/*
 * The board has buttons that are connected to ADC pins which pressed and
 * released values are determined by the analog voltage
 */
#undef CONFIG_ADC_BUTTONS

/*
 * Allow runtime configuration of the buttons[] array
 */
#undef CONFIG_BUTTONS_RUNTIME_CONFIG

/* Support simulation of a button press using EC tool command */
#undef CONFIG_HOSTCMD_BUTTON
/*
 * Configuration for button simulation i.e. dependent on
 * CONFIG_HOSTCMD_BUTTON or CONFIG_CMD_BUTTON config.
 */
#undef CONFIG_SIMULATED_BUTTON

/*
 * Capsense chip has buttons, too.
 */
#undef CONFIG_CAPSENSE

/*****************************************************************************/
/* Support CEC */
#undef CONFIG_CEC

/*****************************************************************************/

/* Compile charge manager */
#undef CONFIG_CHARGE_MANAGER

/*
 * Number of charge ports excluding type-c ports
 *
 * If defined, the board must define a macro DEDICATED_CHARGE_PORT indicates
 * the dedicated port number.
 *
 * See include/charge_manager.h for more details about dedicated port.
 */
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 0

/* Allow charge manager to default to charging from dual-role partners */
#undef CONFIG_CHARGE_MANAGER_DRP_CHARGING

/* Handle the external power limit host command in charge manager */
#undef CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT

/* Initially enter safe mode, with relaxed port / current selection rules */
#define CONFIG_CHARGE_MANAGER_SAFE_MODE

/* Leave safe mode when battery pct meets or exceeds this value */
#define CONFIG_CHARGE_MANAGER_BAT_PCT_SAFE_MODE_EXIT 2

/* The hardware has some input current ramping/back-off mechanism */
#undef CONFIG_CHARGE_RAMP_HW

/* Compile input current ramping support using software control */
#undef CONFIG_CHARGE_RAMP_SW

/*****************************************************************************/
/* Charger config */

/* Compile common charge state code. */
#undef CONFIG_CHARGER

/* Compile charger-specific code for these chargers (pick at most one) */
#undef CONFIG_CHARGER_BD9995X
#undef CONFIG_CHARGER_BQ24715
#undef CONFIG_CHARGER_BQ24770
#undef CONFIG_CHARGER_BQ24773
#undef CONFIG_CHARGER_BQ25710
#undef CONFIG_CHARGER_ISL9237
#undef CONFIG_CHARGER_ISL9238 /* For ISL9238 A/B */
#undef CONFIG_CHARGER_ISL9238C
#undef CONFIG_CHARGER_ISL9241
#undef CONFIG_CHARGER_MT6370
#undef CONFIG_CHARGER_RAA489000
#undef CONFIG_CHARGER_RT9466
#undef CONFIG_CHARGER_RT9467
#undef CONFIG_CHARGER_SM5803
#undef CONFIG_CHARGER_SY21612


/* Allow run-time completion of the charger driver structure */
#undef CONFIG_CHARGER_RUNTIME_CONFIG

/*
 * Board has only one charger chip (default, undef when board contains multiple
 * charger chips
 */
#define CONFIG_CHARGER_SINGLE_CHIP

/*
 * Enable the CHG_EN at initialization to turn-on the BGATE which allows voltage
 * to be applied to the battery PACK & wakes the battery if it is in shipmode.
 */
#undef CONFIG_CHARGER_BD9995X_CHGEN

/*
 * BD9995X Power Save Mode
 *
 * Which power save mode should the charger enter when VBUS is removed.  Check
 * driver/bd9995x.h for the power save settings.  By default, no power save mode
 * is enabled.
 */
#undef CONFIG_BD9995X_POWER_SAVE_MODE

/*
 * If the battery temperature sense pin is connected to charger,
 * get the battery temperature from the charger.
 */
#undef CONFIG_CHARGER_BATTERY_TSENSE

/*
 * Board specific charging current limit, in mA.  If defined, the charge state
 * machine will not allow the battery to request more current than this.
 */
#undef CONFIG_CHARGER_CURRENT_LIMIT

/*
 * MT6370 backlight control settings.
 * If defined, Panel backlight power is controlled by MT6370.
 */
#undef CONFIG_CHARGER_MT6370_BACKLIGHT

/*
 * MT6360/MT6370 BC1.2 USB-PHY control.
 * If defined, USB-PHY connection is controlled by GPIO_BC12_DET_EN.
 * Assert GPIO_BC12_DET_EN to detect BC1.2 device, and deassert
 * GPIO_BC12_DET_EN to mux USB-PHY back.
 */
#undef CONFIG_MT6360_BC12_GPIO
#undef CONFIG_CHARGER_MT6370_BC12_GPIO

/*
 * Enable/disable system power monitor PSYS function: this enables output
 * from charger chip to SoC.
 */
#undef CONFIG_CHARGER_PSYS

/*
 * Enable reading PSYS (system power) value, either via "psys" console command,
 * or via charger_get_system_power function.
 */
#undef CONFIG_CHARGER_PSYS_READ

/*
 * Board supports discharge mode.  In this mode, the battery will discharge
 * even if AC is present.  Used for testing.
 */
#undef CONFIG_CHARGER_DISCHARGE_ON_AC

/* Board has a custom discharge mode. */
#undef CONFIG_CHARGER_DISCHARGE_ON_AC_CUSTOM

/*
 * Board specific flag used to disable external ILIM pin used to determine input
 * current limit. When defined, the input current limit is decided only by
 * the software register value.
 */
#undef CONFIG_CHARGER_ILIM_PIN_DISABLED

/*
 * Default input current for the board, in mA.  Many boards also use this as the
 * least maximum input current during transients.
 *
 * This value should depend on external power adapter, designed charging
 * voltage, and the maximum power of the running system. For type-C chargers,
 * this should be set to 512 mA in order to not brown-out low-current USB
 * charge ports in accordance with USB-PD r3.0 Sec. 7.3
 */
#undef CONFIG_CHARGER_INPUT_CURRENT

/*
 * This config option is used to enable IDCHG trigger for prochot. This macro
 * should be set to the desired current limit to draw from the battery before
 * triggering prochot. Note that is has a 512 mA granularity. The function that
 * sets the limit will mask of the lower 10 bits. For this check to be active
 * the bq25710 must be in performance mode and this config option is also used
 * to keep the bq25710 in performance mode when the AP is in S0.
 */
#undef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA

/*
 * Board specific maximum input current limit, in mA.
 */
#undef CONFIG_CHARGER_MAX_INPUT_CURRENT

/*
 * Leave charger VBAT configured to battery-requested voltage under all
 * conditions, even when AC is not present. This may be necessary to work
 * around quirks of certain charger chips, such as the BD9995X.
 */
#undef CONFIG_CHARGER_MAINTAIN_VBAT

/*
 * Power thresholds for AP boot
 *
 * If one of the following conditions is met, EC boots AP:
 *
 * 1. Battery charge >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
 * 2. AC power >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
 * 3. Battery charge >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC
 *    and
 *    AC power >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT
 *
 * Note that CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT/_CHG_MW are thresholds
 * for the OS boot used by Depthcharge. The OS has higher power requirement
 * but PD power is also available.
 *
 * WARNING: Locked RO firmware does not negotiate power greater than 15W via
 * analog signaling.  If the AP requires greater than 15W to boot, then see
 * CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW.
 */
#undef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
#undef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC
/* Default: 15000 */
#undef CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
/* Default: Disabled */
#undef CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT

/* Minimum battery percentage for power on with an imbalanced pack */
#undef CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON

/*
 * Maximum battery cell imbalance to accept before considering the pack to be
 * imbalanced, in millivolts.
 */
#undef CONFIG_BATTERY_MAX_IMBALANCE_MV

/* Set this option when using a Narrow VDC (NVDC) charger, such as ISL9237/8. */
#undef CONFIG_CHARGER_NARROW_VDC

/*
 * Low energy thresholds - when battery level is below BAT_PCT and an external
 * charger provides less than CHG_MW of power, inform the AP of the situation
 * through the LIMIT_POWER charge state parameter.  Depthcharge will hold off on
 * the boot for up to 3 seconds while waiting for either condition to clear
 * before starting the kernel.  This wait happens after sw sync in RW mode, so
 * the firmware may set it high enough that PD negotiation is required to clear
 * it.
 *
 * Default: Disabled.  Depthcharge is immediately released to boot the kernel.
 *
 * Setting this value to 15001 will require PD negotiation to be complete prior
 * to releasing depthcharge.  During PD negotiation, the charger will be briefly
 * reduced to about 2.5W for a few hundred ms.
 */
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW
/* Default: CHARGER_MIN_BAT_PCT_FOR_POWER_ON */
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT

/*
 * Enable charger's OTG functions, i.e. make it possible to supply output power
 * from battery.
 */
#undef CONFIG_CHARGER_OTG

/*
 * Charger should call battery_override_params() to limit/correct the voltage
 * and current requested by the battery pack before acting on the request.
 */
#undef CONFIG_CHARGER_PROFILE_OVERRIDE

/*
 * Common code for charger profile override. Should be used with
 * CONFIG_CHARGER_PROFILE_OVERRIDE.
 */
#undef CONFIG_CHARGER_PROFILE_OVERRIDE_COMMON

/*
 * Battery voltage threshold ranges for charge profile override.
 * Override it in board.h if battery has multiple threshold ranges.
 */
#define CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES 2

/* Value of the charge sense resistor, in mOhms */
#undef CONFIG_CHARGER_SENSE_RESISTOR

/* Value of the input current sense resistor, in mOhms */
#undef CONFIG_CHARGER_SENSE_RESISTOR_AC

/*
 * Board has an GPIO pin to enable or disable charging.
 *
 * This GPIO should be named GPIO_CHARGER_EN, if active high. Or
 * GPIO_CHARGER_EN_L if active low.
 */
#undef CONFIG_CHARGER_EN_GPIO

/* Charger enable GPIO is active low */
#undef CONFIG_CHARGER_EN_ACTIVE_LOW

/*
 * OCPC - One Charger IC Per Type-C
 *
 * Define this if the board may have multiple charger ICs in the system.  The
 * assumption is that that primary charger is index 0 and is the charger IC
 * connected to the battery FET.  Additionally, `chgnum` is assumed to be the
 * same as the charge port index.
 */
#undef CONFIG_OCPC

/*
 * Boards using OCPC must define this value in order to seed the starting board
 * battery and system resistance between the secondary charger IC and the
 * battery.  This should be at a minimum the Rds(on) resistance of the BFET plus
 * the series sense resistor.
 */
#undef CONFIG_OCPC_DEF_RBATT_MOHMS

/* Enable trickle charging */
#undef CONFIG_TRICKLE_CHARGING

/* Wireless chargers */
#undef CONFIG_WIRELESS_CHARGER_P9221_R7

/* For customer initial use */
#undef CONFIG_CHARGER_CUSTOMER_SETTING

/*
 * Workaround npcx9 A1 chip's bug for download_from_flash API in th booter.
 * This can be removed when A2 chip is available.
 */
#undef CONFIG_WORKAROUND_FLASH_DOWNLOAD_API

/*****************************************************************************/

/*
 * The chip needs to define special SRAM memory regions as linker sections.
 * Those regions are defined in the special-purpose preprocessed file in
 * chip/<chip_name>/memory_regions.inc using the following macro:
 * REGION(name, attributes, start_address, size)
 *
 * Note: these 'special' regions are NOT cleared at startup contrary to .bss.
 */
#undef CONFIG_CHIP_MEMORY_REGIONS

/*
 * Chip needs to do pre-init very early in main(), and provides chip_pre_init()
 * to do so.
 */
#undef CONFIG_CHIP_PRE_INIT

/*
 * Set the caching attributes of one of the RAM regions to uncached.
 *
 * When defined, CONFIG_CHIP_UNCACHED_REGION must be equal to the name of one
 * of the regions defined in memory_regions.inc for CONFIG_CHIP_MEMORY_REGIONS.
 */
#undef CONFIG_CHIP_UNCACHED_REGION

/*
 * When defined, adds a new linker section to store objects that remain resident
 * in ROM/flash. This is useful on ECs that execute all code from RAM and
 * in which the RAM size is smaller than the flash size.
 *
 * Code can force objects into the .init_rom resident section using the
 * __init_rom macro. Objects should accessed using the include/init_rom.h
 * module.
 */
#undef CONFIG_CHIP_INIT_ROM_REGION

/*
 * This is a convenience macro that causes the .data section to link into
 * the ROM/flash resident section defined above.
 *
 * When enabled, the EC initialization code copies the .data section directly
 * from flash into data RAM.
 *
 * When this is not defined, the bootloader copies the .data section from flash
 * to code RAM. The EC initialization code copies .data from code RAM to data
 * RAM.
 *
 * This is automatically enabled when both CONFIG_CHIP_INIT_ROM_REGION and
 * CONFIG_MAPPED_STORAGE are enabled.
 */
#undef CONFIG_CHIP_DATA_IN_INIT_ROM

/*****************************************************************************/
/* Chipset config */

/* AP chipset support; pick at most one */
#undef CONFIG_CHIPSET_APOLLOLAKE	/* Intel Apollolake (x86) */
#undef CONFIG_CHIPSET_BRASWELL		/* Intel Braswell (x86) */
#undef CONFIG_CHIPSET_CANNONLAKE	/* Intel Cannonlake (x86) */
#undef CONFIG_CHIPSET_COMETLAKE		/* Intel Cometlake (x86) */
#undef CONFIG_CHIPSET_COMETLAKE_DISCRETE	/* Intel Cometlake (x86),
						 * discrete EC control
						 */
#undef CONFIG_CHIPSET_ECDRIVEN		/* Mock power module */
#undef CONFIG_CHIPSET_GEMINILAKE	/* Intel Geminilake (x86) */
#undef CONFIG_CHIPSET_ICELAKE		/* Intel Icelake (x86) */
#undef CONFIG_CHIPSET_JASPERLAKE	/* Intel Jasperlake (x86) */
#undef CONFIG_CHIPSET_MT817X		/* MediaTek MT817x */
#undef CONFIG_CHIPSET_MT8183		/* MediaTek MT8183 */
#undef CONFIG_CHIPSET_MT8192		/* MediaTek MT8192 */
#undef CONFIG_CHIPSET_RK3288		/* Rockchip rk3288 */
#undef CONFIG_CHIPSET_RK3399		/* Rockchip rk3399 */
#undef CONFIG_CHIPSET_SKYLAKE		/* Intel Skylake (x86) */
#undef CONFIG_CHIPSET_SC7180            /* Qualcomm SC7180 */
#undef CONFIG_CHIPSET_SDM845            /* Qualcomm SDM845 */
#undef CONFIG_CHIPSET_STONEY		/* AMD Stoney (x86)*/
#undef CONFIG_CHIPSET_TIGERLAKE		/* Intel Tigerlake (x86) */

/* Shared chipset support; automatically gets defined below. */
#undef CONFIG_CHIPSET_APL_GLK		/* Apollolake & Geminilake */

/* Support chipset throttling */
#undef CONFIG_CHIPSET_CAN_THROTTLE

/* Enable additional chipset debugging */
#undef CONFIG_CHIPSET_DEBUG

/* Enable chipset reset hook, requires a deferrable function */
#undef CONFIG_CHIPSET_RESET_HOOK

/*
 * Enable chipset resume init and suspend complete hooks. These hooks are
 * usually used to initialize/disable the SPI driver, which goes to sleep
 * on suspend. Require to initialize it first such that it can receive a
 * host resume event, that notifies the normal resume hook.
 */
#undef CONFIG_CHIPSET_RESUME_INIT_HOOK

/*
 * Enable turning on PP3300_A rail before PP5000_A rail on the Ice Lake
 * and Tiger Lake chipsets. Enable this option if there is leakage from PP5000_A
 * resources into PP3300_A resources.
 */
#undef CONFIG_CHIPSET_PP3300_RAIL_FIRST

/*
 * Enable the EC to drive SLP_S3_L during the G3 to S3 transition. This is
 * needed on TigerLake designs to prevent a glitch on the SLP_S3_L and PCH_PWROK
 * signals during power on.
 */
#undef CONFIG_CHIPSET_SLP_S3_L_OVERRIDE

/*
 * Enable if chipset requires delay between power signals going high
 * and deasserting RSMRST to PCH.
 */
#undef CONFIG_CHIPSET_X86_RSMRST_DELAY

/* Support PMIC reset(using LDO_EN) in chipset */
#undef CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET

/* Board requires chipset pre-init callback */
#undef CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK

/* Redefine when we need a different power-on sequence on the same chipset. */
#define CONFIG_CHIPSET_POWER_SEQ_VERSION 0

/*****************************************************************************/
/*
 * Chip config for clock circuitry
 *	define = crystal / undef = oscillator
 */
#undef CONFIG_CLOCK_CRYSTAL

/*
 * Indicate if a clock source is connected to stm32f4's high speed external
 * clock signal (HSE) specific input
 */
#undef CONFIG_STM32_CLOCK_HSE_HZ

/*
 * Indicate if a clock source is connected to low speed external (LSE) specific
 * input
 */
#undef CONFIG_STM32_CLOCK_LSE

/*
 * Chip config for clock source
 *	 define = external crystal oscillator / undef = internal clock source
 */
#undef CONFIG_CLOCK_SRC_EXTERNAL

/*****************************************************************************/
/* Support curve25519 public key cryptography */
#undef CONFIG_CURVE25519

/*****************************************************************************/
/* PMIC config */

/* Support firmware long press power-off timer */
#undef CONFIG_PMIC_FW_LONG_PRESS_TIMER

/* Support PMIC power control */
#undef CONFIG_PMIC

/*****************************************************************************/
/*
 * Example functionality that is used to test boards.
 */

/*
 * Enable the blink example.
 *
 * LEDs are used to count in binary.
 *
 * Required Configuration:
 * - CONFIG_BLINK_LEDS        --> List of LEDs (gpio enum names) to use as bits
 */
#undef  CONFIG_BLINK
#undef  CONFIG_BLINK_LEDS                     /* Ex: GPIO_LED1, GPIO_LED2 */

/*****************************************************************************/
/*
 * Optional console commands
 *
 * Defining these options will enable the corresponding command on the EC
 * console.
 */

#undef  CONFIG_CMD_ACCELS
#undef  CONFIG_CMD_ACCEL_FIFO
#undef  CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_ACCELSPOOF
#define CONFIG_CMD_ADC
#undef  CONFIG_CMD_ALS
#define CONFIG_CMD_APTHROTTLE
#undef  CONFIG_CMD_BATDEBUG
#define CONFIG_CMD_BATTFAKE
#undef  CONFIG_CMD_BATT_MFG_ACCESS
#define CONFIG_CMD_RETIMER
#undef  CONFIG_CMD_BUTTON
#define CONFIG_CMD_CBI
#undef  CONFIG_CMD_CHARGEN
#define CONFIG_CMD_CHARGER
#undef  CONFIG_CMD_CHARGER_ADC_AMON_BMON
#undef  CONFIG_CMD_CHARGER_DUMP
#undef  CONFIG_CMD_CHARGER_PROFILE_OVERRIDE
#undef  CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST
#define CONFIG_CMD_CHARGE_SUPPLIER_INFO
#undef  CONFIG_CMD_CHGRAMP
#undef  CONFIG_CMD_CLOCKGATES
#undef  CONFIG_CMD_COMXTEST
#define CONFIG_CMD_CRASH
#define CONFIG_CMD_DEVICE_EVENT
#undef  CONFIG_CMD_DLOG
#undef  CONFIG_CMD_ECTEMP
#define CONFIG_CMD_FASTCHARGE
#undef  CONFIG_CMD_FLASH
#define CONFIG_CMD_FLASHINFO
#undef  CONFIG_CMD_FLASH_LOG
#undef  CONFIG_CMD_FLASH_TRISTATE
#undef  CONFIG_CMD_FORCETIME
#undef  CONFIG_CMD_FPSENSOR_DEBUG
#define CONFIG_CMD_GETTIME
#undef  CONFIG_CMD_GPIO_EXTENDED
#undef  CONFIG_CMD_GSV
#undef  CONFIG_CMD_GT7288
#define CONFIG_CMD_HASH
#define CONFIG_CMD_HCDEBUG
#undef  CONFIG_CMD_HOSTCMD
#undef  CONFIG_CMD_I2CWEDGE
#undef  CONFIG_CMD_I2C_PROTECT
#define CONFIG_CMD_I2C_SCAN
#undef  CONFIG_CMD_I2C_STRESS_TEST
#undef  CONFIG_CMD_I2C_STRESS_TEST_ACCEL
#undef  CONFIG_CMD_I2C_STRESS_TEST_ALS
#undef  CONFIG_CMD_I2C_STRESS_TEST_BATTERY
#undef  CONFIG_CMD_I2C_STRESS_TEST_CHARGER
#undef  CONFIG_CMD_I2C_STRESS_TEST_TCPC
#define CONFIG_CMD_I2C_XFER
#define CONFIG_CMD_IDLE_STATS
#undef  CONFIG_CMD_ILIM
#define CONFIG_CMD_INA
#undef  CONFIG_CMD_JUMPTAGS
#define CONFIG_CMD_KEYBOARD
#undef  CONFIG_CMD_LEDTEST
#undef  CONFIG_CMD_LID_ANGLE
#undef  CONFIG_CMD_MCDP
#define CONFIG_CMD_MD
#define CONFIG_CMD_MEM
#define CONFIG_CMD_MMAPINFO
#define CONFIG_CMD_PD
#undef  CONFIG_CMD_PD_DEV_DUMP_INFO
#undef  CONFIG_CMD_PD_FLASH
#define CONFIG_CMD_PECI
#undef  CONFIG_CMD_PLL
#undef  CONFIG_CMD_PMU
#define CONFIG_CMD_POWERINDEBUG
#undef  CONFIG_CMD_POWERLED
#define CONFIG_CMD_PWR_AVG
#define CONFIG_CMD_POWER_AP
#undef  CONFIG_CMD_PPC_DUMP
#undef  CONFIG_CMD_PS2
#undef  CONFIG_CMD_RAND
#define CONFIG_CMD_REGULATOR
#undef  CONFIG_CMD_RESET_FLAGS
#undef  CONFIG_CMD_RTC
#undef  CONFIG_CMD_RTC_ALARM
#define CONFIG_CMD_RW
#undef  CONFIG_CMD_SCRATCHPAD
#undef	CONFIG_CMD_SEVEN_SEG_DISPLAY
#define CONFIG_CMD_SHMEM
#undef  CONFIG_CMD_SLEEP
#define CONFIG_CMD_SLEEPMASK
#define CONFIG_CMD_SLEEPMASK_SET
#undef  CONFIG_CMD_SPI_FLASH
#undef  CONFIG_CMD_SPI_NOR
#undef  CONFIG_CMD_SPI_XFER
#undef  CONFIG_CMD_STACKOVERFLOW
#define CONFIG_CMD_SYSINFO
#define CONFIG_CMD_SYSJUMP
#define CONFIG_CMD_SYSLOCK
#undef  CONFIG_CMD_TASK_RESET
#undef  CONFIG_CMD_TASKREADY
#undef  CONFIG_CMD_TCPC_DUMP
#define CONFIG_CMD_TEMP_SENSOR
#define CONFIG_CMD_TIMERINFO
#define CONFIG_CMD_TYPEC
#undef  CONFIG_CMD_USART_INFO
#define CONFIG_CMD_USBMUX
#undef  CONFIG_CMD_USB_PD_CABLE
#undef  CONFIG_CMD_USB_PD_PE
#define CONFIG_CMD_WAITMS
#undef  CONFIG_CMD_AP_RESET_LOG

/*****************************************************************************/

/* Provide common core code to output panic information without interrupts. */
#define CONFIG_COMMON_PANIC_OUTPUT

/*
 * Store a panic log and halt the system for a software-related reasons, such as
 * stack overflow or assertion failure.
 */
#undef CONFIG_SOFTWARE_PANIC

/*
 * Certain platforms(e.g. eve, poppy) cannot retain panic info in data ram since
 * VCC is powered down on EC reset. On such platforms, panic data needs to be
 * saved/restored to persistent storage by using chip specific
 * implementations. This option can be enabled by those platforms that have and
 * wish to use chip-implemented panic backup/restore functions.
 */
#undef CONFIG_CHIP_PANIC_BACKUP

/*
 * Provide the default GPIO abstraction layer.
 * You want this unless you are doing a really tiny firmware.
 */
#define CONFIG_COMMON_GPIO

/*
 * Provides smaller GPIO names to reduce flash size.  Instead of the 'name'
 * field in GPIO macro it will concat 'port' and 'pin' to reduce flash size.
 */
#undef CONFIG_COMMON_GPIO_SHORTNAMES

/*
 * Control the IO pins of IO expander via IO Expander APIs
 *
 * If defined, declare the IOEX pin with macro IOEX. For example:
 *     IOEX(IO_NAME, EXPIN(0, 0, 0), GPIO_OUT_HIGH)
 * For more details, see gpio_list.h.
 *
 * WARNING: make sure none of IOEX IOs are accessed at interrupt level / with
 * interrupts disabled. Doing so may hang the EC because IO expanders may rely
 * on I2C interrupts.
 *
 * Some reasons that not unify the GPIO and IOEX APIs have been disscussed and
 * filed in the crbug.com/985540.
 */
#undef CONFIG_IO_EXPANDER

/*
 * EC's supporting powering down GPIO pins.
 * Add flag GPIO_POWER_DOWN and additional API's.
 */
#undef CONFIG_GPIO_POWER_DOWN

/*
 * Provide common runtime layer code (tasks, hooks ...)
 * You want this unless you are doing a really tiny firmware.
 */
#define CONFIG_COMMON_RUNTIME

/* Provide common core code to handle the operating system timers. */
#define CONFIG_COMMON_TIMER

/*****************************************************************************/

/*
 * Make it possible for console to be output to different channels that can be
 * turned on and off.
 *
 * This is useful as a developer convenience when the console is crowded with
 * messages, to make it easier to use the interactive console.
 * FAFT and servod also use this feature.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_CONSOLE_CHANNEL

/*
 * Provide additional help on console commands, such as the supported
 * options/usage.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_CONSOLE_CMDHELP

/*
 * Add a .flags field to the console commands data structure, to distinguish
 * some commands from others. The available flags bits are defined in
 * include/console.h
 */
#undef CONFIG_CONSOLE_COMMAND_FLAGS

/*
 * One use of the .flags field is to make some console commands restricted, so
 * that they can be disabled or enabled at run time.
 */
#undef CONFIG_RESTRICTED_CONSOLE_COMMANDS

/* The default .flags field value is zero, unless overridden with this. */
#undef CONFIG_CONSOLE_COMMAND_FLAGS_DEFAULT

/*
 * Enable EC_CMD_CONSOLE_READ V1. One could disable this config to prevent
 * kernel from creating the `console_log` debugfs entry.
 */
#define CONFIG_CONSOLE_ENABLE_READ_V1

/*
 * Number of entries in console history buffer.
 *
 * Boards may #undef this to reduce memory usage.
 */
#define CONFIG_CONSOLE_HISTORY 8

/* Max length of a single line of input */
#define CONFIG_CONSOLE_INPUT_LINE_SIZE 80

/* Enable verbose output to UART console and extra timestamp print precision. */
#define CONFIG_CONSOLE_VERBOSE

/*****************************************************************************/
/* Support for EC-EC communication */

/*
 * Board is master or slave in EC-EC communication.
 */
#undef CONFIG_EC_EC_COMM_MASTER
#undef CONFIG_EC_EC_COMM_SLAVE

/*
 * Board support battery-related functions in EC-EC communication.
 */
#undef CONFIG_EC_EC_COMM_BATTERY

/*
 * Enable the experimental console.
 *
 * NOTE: If you enable this experimental console, you will need to run the
 * EC-3PO interactive console in the util directory!  Otherwise, you won't be
 * able to enter any commands.
 */
#undef CONFIG_EXPERIMENTAL_CONSOLE

/* Include CRC-8 utility function */
#undef CONFIG_CRC8

/*****************************************************************************/
/*
 * Debugging config
 *
 * Note that these options are enabled by default, because they're really
 * handy for debugging systems during bringup and even at factory time.
 *
 * A board may undefine any or all of these to reduce image size and RAM usage,
 * at the cost of debuggability.
 */

/*
 * ASSERT() macros are checked at runtime.  See CONFIG_DEBUG_ASSERT_REBOOTS
 * to see what happens if one fails.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_DEBUG_ASSERT

/*
 * Prints a message and reboots if an ASSERT() macro fails at runtime.  When
 * enabled, an ASSERT() which fails will produce a message of the form:
 *
 * ASSERTION FAILURE '<expr>' in function() at file:line
 *
 * If this is not defined, failing ASSERT() will trigger a BKPT instruction
 * instead.
 *
 * Ignored if CONFIG_DEBUG_ASSERT is not defined.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_DEBUG_ASSERT_REBOOTS

/*
 * On assertion failure, prints only the file name and the line number.
 *
 * Ignored if CONFIG_DEBUG_ASSERT_REBOOTS is not defined.
 *
 * Boards may define this to reduce image size.
 */
#undef CONFIG_DEBUG_ASSERT_BRIEF

/*
 * Disable the write buffer used for default memory map accesses.
 * This turns "Imprecise data bus errors" into "Precise" errors
 * in exception traces at the cost of some performance.
 * This may help identify the offending instruction causing an
 * exception. Supported on cortex-m.
 */
#undef CONFIG_DEBUG_DISABLE_WRITE_BUFFER

/*
 * Print additional information when exceptions are triggered, such as the
 * fault address, here shown as bfar. This shows the reason for the fault
 * and may help to determine the cause.
 *
 *	=== EXCEPTION: 03 ====== xPSR: 01000000 ===========
 *	r0 :0000000b r1 :00000047 r2 :60000000 r3 :200013dd
 *	r4 :00000000 r5 :080053f4 r6 :200013d0 r7 :00000002
 *	r8 :00000000 r9 :200013de r10:00000000 r11:00000000
 *	r12:00000000 sp :200009a0 lr :08002b85 pc :08003a8a
 *	Precise data bus error, Forced hard fault, Vector catch, bfar = 60000000
 *	cfsr = 00008200, shcsr = 00000000, hfsr = 40000000, dfsr = 00000008
 *
 * If this is not defined, only a register dump will be printed.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_DEBUG_EXCEPTIONS

/*
 * Print orientation when device orientation changes
 * (requires CONFIG_SENSOR_ORIENTATION)
 */
#undef CONFIG_DEBUG_ORIENTATION

/* Support Synchronous UART debug printf. */
#undef CONFIG_DEBUG_PRINTF

/* Check for stack overflows on every context switch */
#define CONFIG_DEBUG_STACK_OVERFLOW

/*****************************************************************************/

/* Support events from devices attached to the EC */
#undef CONFIG_DEVICE_EVENT

/* Monitor the states of other devices */
#undef CONFIG_DEVICE_STATE

/* Support DMA transfers inside the EC */
#undef CONFIG_DMA

/* Use the common interrupt handlers for DMA IRQs */
#define CONFIG_DMA_DEFAULT_HANDLERS

/* Compile extra debugging and tests for the DMA module */
#undef CONFIG_DMA_HELP

/*
 * If the board supports DRAM, base DRAM address for the chip, where we want
 * to load extra code/data (address from chip address space).
 */
#undef CONFIG_DRAM_BASE

/*
 * If the board supports DRAM, base DRAM address to load the extra code/data
 * (if loaded by AP, this is the AP physical address space).
 */
#undef CONFIG_DRAM_BASE_LOAD

/* DRAM size. */
#undef CONFIG_DRAM_SIZE

/* Usually, EC capable of sensor speeds up to 250 Hz */
#define CONFIG_EC_MAX_SENSOR_FREQ_DEFAULT_MILLIHZ 250000

/* Maximal EC sampling rate */
#undef CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ

/* Support EC chip internal data EEPROM */
#undef CONFIG_EEPROM

/*
 * Support for sending emulated sysrq events to AP (on designs with a keyboard,
 * sysrq is passed as normal key presses).
 */
#undef CONFIG_EMULATED_SYSRQ

/* Include code for handling external power */
#define CONFIG_EXTPOWER

/* Support detecting external power presence via a GPIO */
#undef CONFIG_EXTPOWER_GPIO

/* Default debounce time for external power signal */
#define CONFIG_EXTPOWER_DEBOUNCE_MS 30

/*****************************************************************************/
/* Number of cooling fans. Undef if none. */
#undef CONFIG_FANS

/* Percentage to which all fans are set at initiation */
#define CONFIG_FAN_INIT_SPEED 100

/* Allow board custom fan control */
#undef CONFIG_CUSTOM_FAN_CONTROL

/* Support fan control while in low-power idle */
#undef CONFIG_FAN_DSLEEP

/*
 * Fans have non-const configuration.
 */
#undef CONFIG_FAN_DYNAMIC

/*
 * Replace the default fan_percent_to_rpm() function with a board-specific
 * implementation in board.c
 */
#undef CONFIG_FAN_RPM_CUSTOM

/*
 * We normally check and update the fans once per second (HOOK_SECOND). If this
 * is #defined to a postive integer N, we will only update the fans every N
 * seconds instead.
 */
#undef CONFIG_FAN_UPDATE_PERIOD

/*****************************************************************************/
/* Flash configuration */

/* This enables console commands and higher-level features */
#define CONFIG_FLASH
/* This enables chip-specific access functions */
#define CONFIG_FLASH_PHYSICAL
#undef CONFIG_FLASH_BANK_SIZE
/* Provide event log stored in flash memory. */
#undef CONFIG_FLASH_LOG
#undef CONFIG_FLASH_LOG_BASE
#undef CONFIG_FLASH_LOG_SPACE
#undef CONFIG_FLASH_ERASED_VALUE32
#undef CONFIG_FLASH_ERASE_SIZE
/* Allow deferred (async) flash erase */
#undef CONFIG_FLASH_DEFERRED_ERASE
/* Flash must be selected for write/erase operations to succeed. */
#undef CONFIG_FLASH_SELECT_REQUIRED

/* Base address of program memory */
#undef CONFIG_PROGRAM_MEMORY_BASE

/*
 * EC code can reside on internal or external storage. Only one of these
 * CONFIGs should be defined. CONFIG_INTERNAL_STORAGE implies XIP
 * (eXecute-In-Place) semantics. i.e. code is being fetched directly from
 * storage media.
 */
#undef CONFIG_EXTERNAL_STORAGE
#undef CONFIG_INTERNAL_STORAGE

/*
 * Flash is directly mapped into the EC's address space.  If this is not
 * defined, the flash driver must implement flash_physical_read().
 */
#define CONFIG_MAPPED_STORAGE

/*
 * Base address of memory-mapped flash storage, for platforms which define
 * CONFIG_MAPPED_STORAGE.
 */
#undef CONFIG_MAPPED_STORAGE_BASE

#undef CONFIG_FLASH_PROTECT_NEXT_BOOT

/*
 * Some platforms need to write protect RW independently of all flash.
 */
#undef CONFIG_FLASH_PROTECT_RW

/*
 * Store persistent write protect for the flash inside the flash data itself.
 * This allows ECs with internal flash to emulate something closer to a SPI
 * flash write protect register.  If this is not defined, write protect state
 * is maintained solely by the physical flash driver.
 */
#define CONFIG_FLASH_PSTATE

/*
 * Store the pstate data in its own dedicated bank of flash.  This allows
 * disabling the protect-RO-at-boot flag without rewriting the RO firmware,
 * but costs a bank of flash.
 *
 * If this is not defined, the pstate data is stored inside the RO firmware
 * image itself.  This is more space-efficient, but the only way to clear the
 * flag once it's set is to rewrite the RO firmware (after removing the WP
 * screw, of course).
 */
#define CONFIG_FLASH_PSTATE_BANK

/*
 * Lock the PSTATE by default (currently only supported when
 * CONFIG_FLASH_PSTATE_BANK is not defined).
 */
#undef CONFIG_FLASH_PSTATE_LOCKED

/*
 * Enable readout protection.
 */
#undef CONFIG_FLASH_READOUT_PROTECTION

/*
 * Use Read-out protection status as PSTATE, i.e. after RDP is enabled, we never
 * allow RO protection to be disabled.
 *
 * This is used when we want to prevent read-back of some critical region (e.g.
 * rollback), even in DFU/BOOT0 mode.
 *
 * Note that this significantly changes the behaviour or flash protection,
 * as this tie EC_FLASH_PROTECT_RO_AT_BOOT with RDP status: it makes no
 * sense to be able to unlock RO protection if RDP is enabled, as a custom RO
 * could allow protected regions readback.
 *
 * TODO(crbug.com/888109): Implementation is currently only available on
 * STM32H7 and STM32F4, and requires more documentation.
 */
#undef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE

/*
 * For flash that is segemented in different regions.
 */
#undef CONFIG_FLASH_MULTIPLE_REGION
/* Number of regions of different size/type */
#undef CONFIG_FLASH_REGION_TYPE_COUNT

/* Total size of writable flash */
#undef CONFIG_FLASH_SIZE

/* Minimum flash write size (in bytes) */
#undef CONFIG_FLASH_WRITE_SIZE
/* Most efficient flash write size (in bytes) */
#undef CONFIG_FLASH_WRITE_IDEAL_SIZE

/* Protected region of storage belonging to EC */
#undef CONFIG_EC_PROTECTED_STORAGE_OFF
#undef CONFIG_EC_PROTECTED_STORAGE_SIZE

/* Writable region of storage belonging to EC */
#undef CONFIG_EC_WRITABLE_STORAGE_OFF
#undef CONFIG_EC_WRITABLE_STORAGE_SIZE

/*****************************************************************************/
/* Fingerprint Sensor Configuration */
#undef CONFIG_FP_SENSOR
#undef CONFIG_FP_SENSOR_FPC1025
#undef CONFIG_FP_SENSOR_FPC1035
#undef CONFIG_FP_SENSOR_FPC1145

/*****************************************************************************/

/* Include a flashmap in the compiled firmware image */
#define CONFIG_FMAP

/* Allow EC serial console input to wake up the EC from STOP mode */
#undef CONFIG_FORCE_CONSOLE_RESUME

/* Enable support for floating point unit */
#undef CONFIG_FPU

/*****************************************************************************/
/* Firmware region configuration */

#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_FW_PSTATE_SIZE

/*
 * Read-only / read-write image configuration.
 * Images may reside on storage (ex. external or internal SPI) at a different
 * offset than when copied to program memory. Hence, two sets of offsets,
 * for STORAGE and for MEMORY.
 */
#undef CONFIG_RO_MEM_OFF
/* Offset relative to CONFIG_EC_PROTECTED_STORAGE_OFF */
#undef CONFIG_RO_STORAGE_OFF
#undef CONFIG_RO_SIZE

#undef CONFIG_RW_MEM_OFF
/* Some targets include two RW sections in the image. */
#undef CONFIG_RW_B
/* This is the offset of the second RW section into the flash. */
#undef CONFIG_RW_B_MEM_OFF

/* Offset relative to CONFIG_EC_WRITABLE_STORAGE_OFF */
#undef CONFIG_RW_STORAGE_OFF
#undef CONFIG_RW_SIZE

/*
 * Offset relative to CONFIG_EC_PROTECTED_STORAGE_OFF
 * These define a region of flash used to store ROM resident data objects
 * for RO images.  This is only possible when the program memory is smaller
 * than CONFIG_EC_PROTECTED_STORAGE_SIZE.
 */
#undef CONFIG_RO_ROM_RESIDENT_MEM_OFF
#undef CONFIG_RO_ROM_RESIDENT_SIZE

/*
 * Offset relative to CONFIG_EC_WRITABLE_STORAGE_OFF
 * These define a region of flash used to store ROM resident data objects
 * for RW images.  This is only possible when the program memory is smaller
 * than CONFIG_EC_WRITABLE_STORAGE_SIZE.
 */
#undef CONFIG_RW_ROM_RESIDENT_MEM_OFF
#undef CONFIG_RW_ROM_RESIDENT_SIZE

/*
 * NPCX-specific bootheader geometry.
 * TODO(crosbug.com/p/23796): Factor these CONFIGs out.
 */
#undef CONFIG_RO_HDR_MEM_OFF
#undef CONFIG_RO_HDR_SIZE

/*
 * Write protect region offset / size. This region normally encompasses the
 * RO image, but may also contain additional images or data.
 */
#undef CONFIG_WP_STORAGE_OFF
#undef CONFIG_WP_STORAGE_SIZE

/*
 * Rollback protect region. If CONFIG_ROLLBACK is defined to enable the rollback
 * protect region, CONFIG_ROLLBACK_OFF and CONFIG_ROLLBACK_SIZE must be defined
 * too.
 */
#undef CONFIG_ROLLBACK
#undef CONFIG_ROLLBACK_OFF
#undef CONFIG_ROLLBACK_SIZE

/* If defined, add support for storing some entropy in the rollback region. */
#undef CONFIG_ROLLBACK_SECRET_SIZE

/* If defined, protect rollback region readback using MPU. */
#undef CONFIG_ROLLBACK_MPU_PROTECT


/*
 * If defined, inject some locally generated entropy when secret is updated,
 * using board_get_entropy function.
 * Large values may take a long time to generate.
 *
 * This is only meant to add a little bit of extra entropy, when the hardware
 * lacks a random number generator (otherwise, the strong entropy can be
 * directly added to the secret, using rollback_add_entropy).
 */
#undef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE

/* If defined, we can update rollback information (RW can unset this). */
#define CONFIG_ROLLBACK_UPDATE

/*
 * Current rollback version. Meaningless for RO (but provides the minimum value
 * that will be written to the rollback protection at flash time).
 *
 * For RW, rollback version included in version structure, used by RO to
 * determine if the RW image is recent enough and can be jumped to.
 *
 * Valid values are >= 0, <= INT32_MAX (positive, 32-bit signed integer).
 */
#define CONFIG_ROLLBACK_VERSION 0

/*
 * Board Image ec.bin contains a RO firmware.  If not defined, the image will
 * only contain the RW firmware.
 */
#define CONFIG_FW_INCLUDE_RO

/* If defined, another image (RW) exists with more features */
#undef CONFIG_FW_LIMITED_IMAGE

/*
 * If defined, we can use system_get_fw_reset_vector function to decide
 * reset vector of RO/RW firmware for sysjump.
 */
#undef CONFIG_FW_RESET_VECTOR

/*****************************************************************************/
/* Motion sensor based gesture recognition information */
/* These all require HAS_TASK_MOTIONSENSE to work */

/* Do we want to detect gestures? */
#undef CONFIG_GESTURE_DETECTION

/* Mask of all sensors used for gesture dectections */
#undef CONFIG_GESTURE_DETECTION_MASK

/* some gesture recognition done in software */
#undef CONFIG_GESTURE_SW_DETECTION

/* enable gesture host interface */
#undef CONFIG_GESTURE_HOST_DETECTION

/* Sensor sampling interval for gesture recognition */
#undef CONFIG_GESTURE_SAMPLING_INTERVAL_MS

/* Which sensor to look for double tap recognition */
#undef CONFIG_GESTURE_SENSOR_DOUBLE_TAP

/* Use for waking up host */
#undef CONFIG_GESTURE_SENSOR_DOUBLE_TAP_FOR_HOST

/*
 * Double tap detection parameters
 * Double tap works by looking for two isolated Z-axis accelerometer impulses
 * preceded and followed by relatively calm periods of accelerometer motion.
 *
 * Define an outer and inner window. The inner window specifies how
 * long the tap impulse is expected to last. The outer window specifies the
 * period before the initial tap impluse and after the final tap impulse for
 * which to check for relatively calm periods. In between the two impulses
 * there is a minimum and maximum interstice time allowed.
 *
 * Define an acceleration threshold to dectect a tap, in mg.
 */
#undef CONFIG_GESTURE_TAP_OUTER_WINDOW_T
#undef CONFIG_GESTURE_TAP_INNER_WINDOW_T
#undef CONFIG_GESTURE_TAP_MIN_INTERSTICE_T
#undef CONFIG_GESTURE_TAP_MAX_INTERSTICE_T
#undef CONFIG_GESTURE_TAP_THRES_MG

/* Which sensor to look for significant motion activity */
#undef CONFIG_GESTURE_SIGMO

/*
 * Significant motion parameters
 * Sigmo state machine looks for movement, waits skip milli-seconds,
 * and check for movement again with proof milli-seconds.
 */
#undef CONFIG_GESTURE_SIGMO_PROOF_MS
#undef CONFIG_GESTURE_SIGMO_SKIP_MS
#undef CONFIG_GESTURE_SIGMO_THRES_MG

/* Support getting gpio flags. */
#undef CONFIG_GPIO_GET_EXTENDED

/* Do we want to detect the lid angle? */
#undef CONFIG_LID_ANGLE

/* Which sensor is located on the base? */
#undef CONFIG_LID_ANGLE_SENSOR_BASE
/* Which sensor is located on the lid? */
#undef CONFIG_LID_ANGLE_SENSOR_LID
/*
 * Allows using the lid angle measurement to determine if peripheral devices
 * should be enabled or disabled, like key scanning, trackpad interrupt.
 */
#undef CONFIG_LID_ANGLE_UPDATE

/*
 * Defer the (re)configuration of motion sensors after the suspend event or
 * resume event.  Sensor power rails may be powered up or down asynchronously
 * from the EC, so it may be necessary to wait some time period before
 * reconfiguring after a transition.
 */
#define CONFIG_MOTION_SENSE_SUSPEND_DELAY_US 0
#define CONFIG_MOTION_SENSE_RESUME_DELAY_US 0

/* Define motion sensor count in board layer */
#undef CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Define when LPC memory space needs to be populated. */
#undef CONFIG_MOTION_FILL_LPC_SENSE_DATA

/******************************************************************************/
/* Host to RAM (H2RAM) Memory Mapping */

/* H2RAM Base memory address */
#undef CONFIG_H2RAM_BASE

/* H2RAM Size */
#undef CONFIG_H2RAM_SIZE

/* H2RAM Host LPC I/O base memory address */
#undef CONFIG_H2RAM_HOST_LPC_IO_BASE

/* ISH boot start address */
#undef CONFIG_ISH_BOOT_START
/*
 * Define the minimal amount of time (in ms) betwen running motion sense task
 * loop.
 */
#define CONFIG_MOTION_MIN_SENSE_WAIT_TIME 3

/*****************************************************************************/
/*
 * Support the host asking the EC about the status of the most recent host
 * command.
 *
 * When the AP is attached to the EC via a serialized bus such as I2C or SPI,
 * it needs a way to minimize the length of time an EC command will tie up the
 * bus (and the kernel driver on the AP).  If this config is defined, the EC
 * may return an in-progress result code for slow commands such as flash
 * erase/write instead of stalling until the command finishes processing, and
 * the AP may then inquire the status of the current command and/or the result
 * of the previous command.
 */
#undef CONFIG_HOST_COMMAND_STATUS

/* clear bit(s) to mask reporting of an EC_HOST_EVENT_XXX event(s) */
#define CONFIG_HOST_EVENT_REPORT_MASK 0xffffffff
#define CONFIG_HOST_EVENT64_REPORT_MASK 0xffffffffffffffffULL

/* Config option to support 64-bit hostevents and wake-masks. */
#define CONFIG_HOST_EVENT64

/*
 * The host commands are sorted in the .rodata.hcmds section so use the binary
 * search algorithm to match a command to its handler
 */
#undef CONFIG_HOSTCMD_SECTION_SORTED

/*
 * Host command parameters and response are 32-bit aligned.  This generates
 * much more efficient code on ARM.
 */
#undef CONFIG_HOSTCMD_ALIGNED

/*
 * Include host commands to fetch battery information from
 * ec_response_battery_static/dynamic_info structures, only makes sense when
 * CONFIG_BATTERY_V2 is enabled.
 */
#undef CONFIG_HOSTCMD_BATTERY_V2

/* Default hcdebug mode, e.g. HCDEBUG_OFF or HCDEBUG_NORMAL */
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_NORMAL

/* If we have host command task, assume we also are using host events. */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_EVENTS
#else
#undef  CONFIG_HOSTCMD_EVENTS
#endif

/*
 * Board supports host command to get EC SPI flash info.  This is typically
 * only needed if the factory needs to determine which of several possible SPI
 * flash chips is attached to the EC on a given board.
 */
#undef CONFIG_HOSTCMD_FLASH_SPI_INFO

/*
 * For ECs where the host command interface is I2C, slave
 * address which the EC will respond to.
 */
#undef CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS

/*
 * Accept EC host commands over the SPI slave (SPS) interface.
 */
#undef CONFIG_HOSTCMD_SPS

/*
 * Host command rate limiting assures EC will have time to process lower
 * priority tasks even if the AP is hammering the EC with host commands.
 * If there is less than CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST between
 * host commands for CONFIG_HOSTCMD_RATE_LIMITING_PERIOD, then a
 * recess period of CONFIG_HOSTCMD_RATE_LIMITING_RECESS will be
 * enforced.
 */
#define CONFIG_HOSTCMD_RATE_LIMITING_PERIOD   (500 * MSEC)
#define CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST (3   * MSEC)
#define CONFIG_HOSTCMD_RATE_LIMITING_RECESS   (20  * MSEC)

/* PD MCU supports host commands */
#undef CONFIG_HOSTCMD_PD

/* EC supports EC_CMD_PD_CHIP_INFO */
#define CONFIG_EC_CMD_PD_CHIP_INFO

/*
 * Use if PD MCU controls charging (selecting charging port and input
 * current limit).
 */
#undef CONFIG_HOSTCMD_PD_CHG_CTRL

/* Panic when status of PD MCU reflects that it has crashed */
#undef CONFIG_HOSTCMD_PD_PANIC

/* Board supports RTC host commands */
#undef CONFIG_HOSTCMD_RTC

/* For access to VBNV on-EC battery-backed storage */
#undef CONFIG_HOSTCMD_VBNV_CONTEXT

/* EC controls the board's SKU ID and can report that to the AP */
#undef CONFIG_HOSTCMD_SKUID

/* Set SKU ID from AP */
#undef CONFIG_HOSTCMD_AP_SET_SKUID

/* Command to issue AP reset */
#undef CONFIG_HOSTCMD_AP_RESET

/*
 * Support voltage regulator host command
 * If defined, the board should also implement board functions defined in
 * include/regulator.h
 */
#undef CONFIG_HOSTCMD_REGULATOR

/* Flash commands over PD */
#define CONFIG_HOSTCMD_FLASHPD

/* Host command to control USB-PD chip */
#undef CONFIG_HOSTCMD_PD_CONTROL

/* Set entry in PD MCU's device rw_hash table */
#define CONFIG_HOSTCMD_RWHASHPD

#if !defined(TEST_BUILD) && !defined(TEST_FUZZ)
/* Enable EC_CMD_LOCATE_CHIP */
#define CONFIG_HOSTCMD_LOCATE_CHIP
#endif

/* Command to get the EC uptime (and optionally AP reset stats) */
#define CONFIG_HOSTCMD_GET_UPTIME_INFO

/* List of host commands whose debug output will be suppressed */
#undef CONFIG_SUPPRESSED_HOST_COMMANDS

/*****************************************************************************/

/* Enable debugging and profiling statistics for hook functions */
#undef CONFIG_HOOK_DEBUG

/*****************************************************************************/
/* CRC configuration */

/* Enable the hardware accelerator for CRC computation */
#undef CONFIG_HW_CRC

/* Enable the software routine for CRC computation */
#undef CONFIG_SW_CRC

/*****************************************************************************/

/* Enable system hibernate */
#define CONFIG_HIBERNATE

/* Default delay after shutting down before hibernating */
#define CONFIG_HIBERNATE_DELAY_SEC 3600

/* For ECs with multiple wakeup pins, define enabled wakeup pins */
#undef CONFIG_HIBERNATE_WAKEUP_PINS

/*
 * If defined, chip hibernation is used. Your board needs to define wake-up
 * signals. Undefine this to use board hibernation capability.
 */
#define CONFIG_SUPPORT_CHIP_HIBERNATION

/*
 * Use PSL (Power Switch Logic) for hibernating. It turns off VCC power rail
 * for ultra-low power consumption and uses PSL inputs rely on VSBY power rail
 * to wake up ec and the whole system.
 */
#undef CONFIG_HIBERNATE_PSL

/* Wake up pins have non-const configuration. */
#undef CONFIG_HIBERNATE_WAKE_PINS_DYNAMIC

/* In npcx9 and later chips, enhanced PSL features are supported including:
 *   (1) Pulse mode for PSL_OUT signal.
 *   (2) Open-drain for PSL_OUT signal (when Pulse mode is enabled.)
 * These features can be enabled in board configuration file by adding
 * the following bit masks to this flag:
 *   (1) NPCX_PSL_CFG_PSL_OUT_PULSE.
 *   (2) NPCX_PSL_CFG_PSL_OUT_OD.
 * Ex:  #define CONFIG_HIBERNATE_PSL_OUT_FLAGS	 \
		 (NPCX_PSL_CFG_PSL_OUT_PULSE | NPCX_PSL_CFG_PSL_OUT_OD)
 */
#undef CONFIG_HIBERNATE_PSL_OUT_FLAGS

/*
 * Enable VCC1_RST pin as the input of PSL wakeup source. When Enabling this,
 * the VCC1_RST pin must be connected to the VSBY supply via an external pull-up
 * resistor of maximum 100K ohm .
 * TODO: Remove this when NPCX9 A2 chip is available because A2
 * chip will enable VCC1_RST to PSL wakeup source and lock it in
 * the booter.
 */
#undef CONFIG_HIBERNATE_PSL_VCC1_RST_WAKEUP

/*
 * Chip supports a 64-bit hardware timer and implements
 * __hw_clock_source_read64 and __hw_clock_source_set64.
 *
 * Chips with this config enabled may optionally define
 * __hw_clock_source_read as a 32-bit set function for
 * latency-sensitive situations.
 */
#undef CONFIG_HWTIMER_64BIT

/* Use a hardware specific udelay(). */
#undef CONFIG_HW_SPECIFIC_UDELAY

/*****************************************************************************/
/* I2C configuration */

#undef CONFIG_I2C
#undef CONFIG_I2C_DEBUG
#undef CONFIG_I2C_DEBUG_PASSTHRU
#undef CONFIG_I2C_PASSTHRU_RESTRICTED
#undef CONFIG_I2C_VIRTUAL_BATTERY

/*
 * Define this option if an i2c bus may be unpowered at a certain point during
 * runtime.  An example could be, a sensor bus which is not needed in lower
 * power states so the power rail for those sensors is completely disabled.
 *
 * If defined, your board must provide a board_is_i2c_port_powered() function.
 */
#undef CONFIG_I2C_BUS_MAY_BE_UNPOWERED

/*
 * Conservative I2C reading size per single transaction. For example, register
 * of stm32f0 and stm32l4 are limited to be 8 bits for this field.
 */
#define CONFIG_I2C_CHIP_MAX_READ_SIZE 255

/*
 * Enable i2c_xfer() for receiving request larger than
 * CONFIG_I2C_CHIP_MAX_READ_SIZE.
 */
#undef CONFIG_I2C_XFER_LARGE_READ

/*
 * If defined, makes i2c_xfer callback into board-provided functions before the
 * start and after the end of every I2C transaction. This can be used by boards
 * to implement any I2C device specific quirks e.g. requiring minimum bus-free
 * time between every I2C transaction with a device.
 */
#undef CONFIG_I2C_XFER_BOARD_CALLBACK

/*
 * EC uses an I2C master interface.
 * Note: if this is defined, i2c_init() will be called
 * automatically at board boot.
 */
#undef CONFIG_I2C_MASTER

/* EC uses an I2C slave interface */
#undef CONFIG_I2C_SLAVE

/* Defines I2C operation retry count when slave nack'd(EC_ERROR_BUSY) */
#define CONFIG_I2C_NACK_RETRY_COUNT 0
/*
 * I2C SCL gating.
 *
 * If CONFIG_I2C_SCL_GATE_ADDR/PORT is defined, whenever the defined address
 * is addressed, CONFIG_I2C_SCL_GATE_GPIO is set to high. When the I2C
 * transaction is done, the pin is set back to low.
 */
#undef CONFIG_I2C_SCL_GATE_PORT
#undef CONFIG_I2C_SCL_GATE_ADDR_FLAGS
#undef CONFIG_I2C_SCL_GATE_GPIO

/*
 * Some chip supports two owned slave address. The second slave address is used
 * for other purpose such as board specific i2c commands. This option can be
 * set if user of the second slave address requires larger host packet buffer
 * size.
 */
#define CONFIG_I2C_EXTRA_PACKET_SIZE 0

/*
 * I2C multi-port controller.
 *
 * If CONFIG_I2C_MULTI_PORT_CONTROLLER is defined, a single on-chip I2C
 * controller may have multiple I2C ports attached. Therefore, I2c operations
 * must lock the controller (not just the port) to prevent hardware access
 * conflicts.
 */
#undef CONFIG_I2C_MULTI_PORT_CONTROLLER

/*
 * Enable I2C bitbang driver.
 *
 * If defined, the board must define array i2c_bitbang_ports[] and
 * i2c_bitbang_ports_count (same as i2c_ports/i2c_ports_count), but with
 * port number starting from I2C_PORT_COUNT, and .drv=&bitbang_drv.
 *
 * For example:
 * {"battery", 2, 100, GPIO_I2C3_SCL, GPIO_I2C3_SDA, .drv = &bitbang_drv},
 */
#undef CONFIG_I2C_BITBANG

/*
 * If defined, reduce I2C traffic from update functions (i2c_update8/16
 * and i2c_field_update8/16) by skipping the write if the new value is
 * unchanged from the old value. This assumes no side effects from writing an
 * unchanged value back out.
 */
#undef CONFIG_I2C_UPDATE_IF_CHANGED

/*
 * Packet error checking support for SMBus.
 *
 * If defined, adds error checking support for i2c_readN, i2c_writeN,
 * i2c_read_string and i2c_write_block. Where
 * - write operation appends an error checking byte at end of transfer, and
 * - read operatoin verifies the correctness of error checking byte from the
 * slave.
 * Set I2C_FLAG on addr_flags parameter to use this feature.
 *
 * This option also enables error checking function on smart batteries.
 */
#undef CONFIG_SMBUS_PEC

/* Support I2C HID touchpad interface. */
#undef CONFIG_I2C_HID_TOUCHPAD

/*
 * Add hosts-side support for entering programming mode for I2C ITE ECs.
 * Must define ite_dfu_config_t for configuration in board file.
 */
#undef CONFIG_ITE_FLASH_SUPPORT

/*****************************************************************************/
/* IPI configuration.  Support mt_scp only for now. */

/* EC support Inter-Processor Interrupt. */
#undef CONFIG_IPI

/*
 * IPC0/IPI shared object address. This is the starting address of the send
 * object and the receive object.  Each object contains a buffer.
 */
#undef CONFIG_IPC_SHARED_OBJ_ADDR

/* "buffer" size of ipc_shared_obj. */
#undef CONFIG_IPC_SHARED_OBJ_BUF_SIZE

/* EC support rpmsg name service over IPI. */
#undef CONFIG_RPMSG_NAME_SERVICE

/*****************************************************************************/
/* Current/Power monitor */

/*
 * Compile driver for INA219 or INA231 or INA3221.
 * Only one of these may be defined (if any).
 */
#undef CONFIG_INA219
#undef CONFIG_INA231
#undef CONFIG_INA3221


/*****************************************************************************/
/* Inductive charging */

/* Enable inductive charging support */
#undef CONFIG_INDUCTIVE_CHARGING

/******************************************************************************/

/* Support IT8801 I/O expander. */
#undef CONFIG_IO_EXPANDER_IT8801

/* Support Nuvoton NCT38xx I/O expander. */
#undef CONFIG_IO_EXPANDER_NCT38XX

/* Support NXP PCA9534 I/O expander. */
#undef CONFIG_IO_EXPANDER_PCA9534

/* Support NXP PCAL6408 I/O expander. */
#undef CONFIG_IO_EXPANDER_PCAL6408

/* Number of IO Expander ports */
#undef CONFIG_IO_EXPANDER_PORT_COUNT

/*****************************************************************************/

/* Number of IRQs supported on the EC chip */
#undef CONFIG_IRQ_COUNT

/* Enable LDN for KBC mouse */
#undef CONFIG_IT83XX_ENABLE_MOUSE_DEVICE

/*
 * The IT8320 supports e-flash clock up to 48 MHz (IT8390 maximum is 32 MHz).
 * Enable it if we want better performance of fetching instruction from e-flash.
 *
 * This is valid with PLL frequency equal to 48/96MHz only.
 */
#undef CONFIG_IT83XX_FLASH_CLOCK_48MHZ

/*
 * Enable it if EC's VBAT won't go low when system's power isn't
 * presented (no battery and no AC)
 * If EC's VSTBY and VBAT(power source of BRAM) aren't connected to the same
 * power rail and VBAT doesn't go low immediately (eg: there is a larger
 * capacitance on the rail) after all power off: PD contract recorded in BRAM
 * won't get cleared (But actually we have unplugged type-c adaptor, so the
 * contract should be cleared).
 */
#undef CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* To define it, if I2C channel C and PECI used at the same time. */
#undef CONFIG_IT83XX_SMCLK2_ON_GPC7

/*
 * If this is not defined, the firmware will revert the JTAG selection
 * triggered by the hardware strap pin.
 * Un-define this flag by default for all real platforms. see (b/129908668)
 * If some boards (Ex:EVB) require JTAG function, they can define it in
 * their board.h
 */
#undef CONFIG_ENABLE_JTAG_SELECTION

/*****************************************************************************/
/* Keyboard config */

/*
 * The Silego reset chip sits in between the EC and the physical keyboard on
 * column 2.  To save power in low-power modes, some Silego variants require
 * the signal to be inverted so that the open-drain output from the EC isn't
 * costing power due to the pull-up resistor in the Silego.
 */
#undef CONFIG_KEYBOARD_COL2_INVERTED

/*
 * Keyboards with the assistant key also move the refresh key matrix to row 3
 * instead of row 2.  This is used by the boot key detection code to determine
 * if the refresh key is held down at boot.
 */
#undef CONFIG_KEYBOARD_REFRESH_ROW3

/*
 * Config KSO to start from a different KSO pin. This is to allow some chips
 * to use alternate functions on KSO pins.
 */
#define CONFIG_KEYBOARD_KSO_BASE 0

/*
 * For certain board configurations, KSI2 or KSI3 will be stuck asserted for all
 * scan columns if the power button is held. We must be aware of this case
 * in order to correctly handle recovery mode key combinations.
 */
#undef CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2
#undef CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI3

/* Enable extra debugging output from keyboard modules */
#undef CONFIG_KEYBOARD_DEBUG

/*
 * Disables the directly connected keyboard pins and drivers on a particular
 * chip. You might want this enabled if the keyboard is indirectly connected
 * to the EC, perhaps through an I2C controller.
 */
#undef CONFIG_KEYBOARD_NOT_RAW

/* The board uses a negative edge-triggered GPIO for keyboard interrupts. */
#undef CONFIG_KEYBOARD_IRQ_GPIO

/* Compile code for 8042 keyboard protocol */
#undef CONFIG_KEYBOARD_PROTOCOL_8042

/*
 * Enable code for chromeos vivaldi keyboard (standard for new chromeos devices)
 * This config only takes effect if CONFIG_KEYBOARD_PROTOCOL_8042 is selected. A
 * chromeos device is Vivaldi compatible if the keyboard matrix complies with:
 * go/vivaldi-matrix
 * Vivaldi code enables:
 *  - A response to EC_CMD_GET_KEYBD_CONFIG command from coreboot
 *  - Boards can specify their custom layout for top keys.
 */
#define CONFIG_KEYBOARD_VIVALDI

/* Compile code for MKBP keyboard protocol */
#undef CONFIG_KEYBOARD_PROTOCOL_MKBP

/* Support keyboard factory test scanning */
#undef CONFIG_KEYBOARD_FACTORY_TEST

/*
 * Keyboard config (struct keyboard_scan_config) is in board.c.  If this is
 * not defined, default values from common/keyboard_scan.c will be used.
 */
#undef CONFIG_KEYBOARD_BOARD_CONFIG

/*
 * Support for boot key combinations (e.g. refresh key being held on boot to
 * trigger recovery).
 */
#define CONFIG_KEYBOARD_BOOT_KEYS

/* Add support for the assistant key. */
#undef CONFIG_KEYBOARD_ASSISTANT_KEY

/* Add support for a switch that indicates if the device is in tablet mode. */
#undef CONFIG_KEYBOARD_TABLET_MODE_SWITCH

/*
 * Minimum CPU clocks between scans.  This ensures that keyboard scanning
 * doesn't starve the other EC tasks of CPU when running at a decreased system
 * clock.
 */
#undef CONFIG_KEYBOARD_POST_SCAN_CLOCKS

/*  Print keyboard scan time intervals. */
#undef CONFIG_KEYBOARD_PRINT_SCAN_TIMES

/*
 * Support for extra runtime key combinations (e.g. alt+volup+h/r for hibernate
 * and warm reboot, respectively).
 */
#define CONFIG_KEYBOARD_RUNTIME_KEYS

/*
 * Allow the board layer keyboard customization. If define, the board layer
 * needs to implement:
 * 1. the function board_keyboard_drive_col() which is used to control
 *    the refresh key column.
 * 2. the scancode_set2 and keycap_label array
 * 3. keyboard_customization.h which is similar to keyboard_config.h
 *
 * Note that if your board has the standard chromeos keyboard layout other
 * than the top row, and you are looking only for top row customization, then
 * you should be looking at overriding board_vivaldi_keybd_config() instead.
 */
#undef CONFIG_KEYBOARD_CUSTOMIZATION

/*
 * implement the Combination key for the actual Fn key
 */
#undef CONFIG_KEYBOARD_CUSTOMIZATION_COMBINATION_KEY

/*
 * Allow board-specific 8042 keyboard callback when a key state is changed.
 */
#undef CONFIG_KEYBOARD_SCANCODE_CALLBACK

/*
 * Call board-supplied keyboard_suppress_noise() function when the debounced
 * keyboard state changes.  Some boards use this to send a signal to the audio
 * codec to suppress typing noise picked up by the microphone.
 */
#undef CONFIG_KEYBOARD_SUPPRESS_NOISE

/*
 * Enable keyboard testing functionality. This enables a message which receives
 * a list of keyscan events from the AP and processes them.  This will cause
 * keypresses to appear on the AP through the same mechanism as a normal
 * keyboard press.
 *
 * This can be used to spoof keyboard events, so is not normally defined,
 * except during internal testing.
 */
#undef CONFIG_KEYBOARD_TEST

/*
 * Enable quasi-bidirectional buffers for KSO pins. It has an open-drain output
 * and a low-impedance pull-up. The low-impedance pull-up is active when ec
 * changes the output data buffers from 0 to 1, thereby reducing the
 * low-to-high transition time.
 */
#undef CONFIG_KEYBOARD_KSO_HIGH_DRIVE

/*
 * Add support for keyboards with language ID pins
 */
#undef CONFIG_KEYBOARD_LANGUAGE_ID

/*
 * Enable keypad (a palm-sized keyboard section usually placed on the far right)
 */
#undef CONFIG_KEYBOARD_KEYPAD

/*
 * Enable the 8042 AUX port. This is typically used for PS/2 mouse devices.
 * You will need to implement send_aux_data_to_device and lpc_aux_put_char.
 */
#undef CONFIG_8042_AUX

/*
 * Support simulate scan code function
 */
#undef CONFIG_SIMULATE_KEYCODE

/*****************************************************************************/

/*
 * Enable IT8801 pwm module.
 */
#undef CONFIG_IO_EXPANDER_IT8801_PWM

/*****************************************************************************/

/* Support common LED interface */
#undef CONFIG_LED_COMMON

/* Standard LED behavior according to spec given that we have a red-green
 * bicolor led for charging and one power led
 */
#undef CONFIG_LED_POLICY_STD

/*
 * Support common PWM-controlled LEDs that conform to the Chrome OS LED
 * behaviour specification.
 */
#undef CONFIG_LED_PWM

/*
 * Support common PWM-controlled LEDs that do not conform to the Chrom OS LED
 * behavior specification
 */
#undef CONFIG_LED_PWM_TASK_DISABLED

/*
 * Here are some recommended color settings by default, but a board can change
 * the colors to one of "enum ec_led_colors" as they see fit.
 */
#define CONFIG_LED_PWM_CHARGE_COLOR EC_LED_COLOR_AMBER
#define CONFIG_LED_PWM_NEAR_FULL_COLOR EC_LED_COLOR_GREEN
#define CONFIG_LED_PWM_CHARGE_ERROR_COLOR EC_LED_COLOR_RED
#define CONFIG_LED_PWM_SOC_ON_COLOR EC_LED_COLOR_GREEN
#define CONFIG_LED_PWM_SOC_SUSPEND_COLOR EC_LED_COLOR_GREEN
#define CONFIG_LED_PWM_LOW_BATT_COLOR EC_LED_COLOR_AMBER

/*
 * By default the PWM LED behaviour is reflected on both LEDs and includes the
 * chipset state, battery state, as well as the charging state.  Enable
 * this CONFIG_* option to show only the charging state on the LEDs.
 */
#undef CONFIG_LED_PWM_CHARGE_STATE_ONLY

/*
 * By default the PWM LED behaviour is reflected on both LEDs and includes the
 * chipset state, battery state, as well as the charging state.  Enable
 * this CONFIG_* option to show only the charging state, and only on the LED of
 * the active charge port.
 */
#undef CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY

/*
 * How many PWM LEDs does the system have that will be controlled by the common
 * PWM LED policy?  Currently, this may be at most 2.
 */
#undef CONFIG_LED_PWM_COUNT

/*
 * Support GPIO-controlled LEDs for common battery/power
 * states through a board-defined lookup table.
 */
#undef CONFIG_LED_ONOFF_STATES

/*
 * Set the battery charge percentage for optional STATE_DISCHARGE_S0_BAT_LOW
 * provided by CONFIG_LED_ONOFF_STATES.
 */
#undef CONFIG_LED_ONOFF_STATES_BAT_LOW

/*
 * Adds a power LED under the control of the board-defined lookup table.
 * Must be used with the CONFIG_LED_ONOFF_STATES option.
 */
#undef CONFIG_LED_POWER_LED

/*
 * LEDs for LED_POLICY STD may be inverted.  In this case they are active low
 * and the GPIO names will be GPIO_LED..._L.
 */
#undef CONFIG_LED_BAT_ACTIVE_LOW
#undef CONFIG_LED_POWER_ACTIVE_LOW

/* Support for LED driver chip(s) */
#undef CONFIG_LED_DRIVER_DS2413  /* Maxim DS2413, on one-wire interface */
#undef CONFIG_LED_DRIVER_LM3509  /* LM3509, on I2C interface */
#undef CONFIG_LED_DRIVER_LM3630A /* LM3630A, on I2C interface */
#undef CONFIG_LED_DRIVER_LP5562  /* LP5562, on I2C interface */
#undef CONFIG_LED_DRIVER_OZ554   /* O2Micro OZ554, on I2C */

/* Offset in flash where little firmware will live. */
#undef CONFIG_LFW_OFFSET

/*
 * Compile lid switch support.
 *
 * This is enabled by default because all boards other than reference boards
 * are for laptops with lid switchs.  Reference boards #undef it.
 */
#define CONFIG_LID_SWITCH

/*
 * GPIOs to use to detect that the lid is opened.
 *
 * This is a X-macro composed of a list of LID_OPEN(GPIO_xxx) elements defining
 * all the GPIOs to check to find whether the lid is currently opened.
 * If not defined, it is using GPIO_LID_OPEN.
 */
#undef CONFIG_LID_SWITCH_GPIO_LIST

/*
 * Support for turning the lightbar power rails on briefly when the AP is off.
 * Enabling this requires implementing the board-specific lb_power() function
 * to do it (see lb_common.h).
 */
#undef CONFIG_LIGHTBAR_POWER_RAILS

/*
 * For tap sequence, show the last segment in dim to give a better idea of
 * battery percentage.
 */
#undef CONFIG_LIGHTBAR_TAP_DIM_LAST_SEGMENT

/*
 * Adds a console command for testing the long long shift right ABI on Cortex-m4
 * (Cr50).
 */
#undef CONFIG_LLSR_TEST

/* Program memory offset for little firmware loader. */
#undef CONFIG_LOADER_MEM_OFF

/* Size of little firmware loader. */
#undef CONFIG_LOADER_SIZE

/* Little firmware loader storage offset. */
#undef CONFIG_LOADER_STORAGE_OFF

/*
 * Low power idle options. These are disabled by default and all boards that
 * want to use low power idle must define it. When using the LFIOSC, the low
 * frequency clock will be used to conserve even more power when possible.
 *
 * GPIOs which need to trigger interrupts in low power idle must specify the
 * GPIO_INT_DSLEEP flag in gpio_list[].
 *
 * Note that for some processors (e.g. LM4), an active JTAG connection will
 * prevent the EC from using low-power idle.
 */
#undef CONFIG_LOW_POWER_IDLE
#undef CONFIG_LOW_POWER_USE_LFIOSC

/* Allows us to enable/disable low power idle mode in runtime. */
#undef CONFIG_LOW_POWER_IDLE_LIMITED

/*
 * Enable deep sleep during S0 (ignores SLEEP_MASK_AP_RUN).
 */
#undef CONFIG_LOW_POWER_S0

/* DMA paging between SRAM and DRAM */
#undef CONFIG_DMA_PAGING

/*
 * Enable HID subsystem using HECI on Intel ISH (Integrated Sensor Hub)
 */
#undef CONFIG_HID_HECI

/* Support host command interface over HECI */
#undef CONFIG_HOSTCMD_HECI

/*
 * EC supports x86 host communication with AP. This can either be through LPC
 * or eSPI. The CONFIG_HOSTCMD_X86 will get automatically defined if either
 * CONFIG_HOSTCMD_LPC or CONFIG_HOSTCMD_ESPI are defined. LPC and eSPI are
 * mutually exclusive.
 */
#undef CONFIG_HOSTCMD_X86
/* Support host command interface over LPC bus. */
#undef CONFIG_HOSTCMD_LPC
/* Support host command interface over eSPI bus. */
#undef CONFIG_HOSTCMD_ESPI

/*
 * SLP signals (SLP_S3 and SLP_S4) use virtual wires intead of physical pins
 * with eSPI interface.
 */
#undef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#undef CONFIG_HOSTCMD_ESPI_VW_SLP_S4

/* MCHP next two items are EC eSPI slave configuration */
/* Maximum clock frequence eSPI EC slave advertises
 * Values in MHz are 20, 25, 33, 50, and 66
 */
#undef CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ

/* EC eSPI slave advertises IO lanes
 * 0 = Single
 * 1 = Single and Dual
 * 2 = Single and Quad
 * 3 = Single, Dual, and Quad
 */
#undef CONFIG_HOSTCMD_ESPI_EC_MODE

/* Bit map of eSPI channels EC advertises
 * bit[0] = 1 Peripheral channel
 * bit[1] = 1 Virtual Wire channel
 * bit[2] = 1 OOB channel
 * bit[3] = 1 Flash channel
 */
#undef CONFIG_HOSTCMD_ESPI_EC_CHAN_BITMAP

/* Base address of low power RAM. */
#undef CONFIG_LPRAM_BASE

/* Size of low power RAM. */
#undef CONFIG_LPRAM_SIZE

/* Use Link-Time Optimizations to try to reduce the firmware code size */
#undef CONFIG_LTO

/* Provide rudimentary malloc/free like services for shared memory. */
#undef CONFIG_MALLOC

/* Need for a math library */
#undef CONFIG_MATH_UTIL

/* Include sensor online calibration (requires CONFIG_FPU) */
#undef CONFIG_ONLINE_CALIB

/*
 * Duration after which an entry in the temperature cache is considered stale.
 * Defaults to 5 minutes if not set.
 */
#undef CONFIG_TEMP_CACHE_STALE_THRES

/* Set minimum temperature for accelerometer calibration. */
#undef CONFIG_ACCEL_CAL_MIN_TEMP

/* Set maximum temperature for accelerometer calibration. */
#undef CONFIG_ACCEL_CAL_MAX_TEMP

/* Set threshold radius for using the Kasa algorithm in accelerometer bias
 * calculation (g).
 */
#undef CONFIG_ACCEL_CAL_KASA_RADIUS_THRES

/* Set threshold radius for using the Newton fit algorithm in accelerometer
 * bias calculation (g).
 */
#undef CONFIG_ACCEL_CAL_NEWTON_RADIUS_THRES

/* Include code to do online compass calibration */
#undef CONFIG_MAG_CALIBRATE

/* Microchip LPC enable debug messages */
#undef CONFIG_MCHP_DEBUG_LPC

/* Microchip I2C controller slave addresses */
#undef CONFIG_MCHP_I2C0_SLAVE_ADDRS
#undef CONFIG_MCHP_I2C1_SLAVE_ADDRS
#undef CONFIG_MCHP_I2C2_SLAVE_ADDRS
#undef CONFIG_MCHP_I2C3_SLAVE_ADDRS

/* Microchip EC SRAM start address */
#undef CONFIG_MEC_SRAM_BASE_START

/* Microchip EC SRAM end address */
#undef CONFIG_MEC_SRAM_BASE_END

/* Microchip EC SRAM size */
#undef CONFIG_MEC_SRAM_SIZE

/*
 * Define Megachips DisplayPort to HDMI protocol converter/level shifter serial
 * interface.
 */
#undef CONFIG_MCDP28X0

/* Define clock input to MFT module. */
#undef CONFIG_MFT_INPUT_LFCLK

/* Minute-IA watchdog timer vector number. */
#define CONFIG_MIA_WDT_VEC 0xFF

/* Support MKBP event */
#undef CONFIG_MKBP_EVENT

/* MKBP events are sent by using host event */
#undef CONFIG_MKBP_USE_HOST_EVENT

/* MKBP events are sent by using GPIO */
#undef CONFIG_MKBP_USE_GPIO

/*
 * MKBP events are notified by using both a GPIO and a host event.
 *
 * You should use this if you are using a GPIO to notify the AP of an MKBP
 * event, and you need an MKBP event to wake the AP in suspend and the AP cannot
 * wake from the GPIO.  Since you are using both a GPIO and a hostevent for the
 * notification, make sure that the S0 hostevent mask does NOT include MKBP
 * events.  Otherwise, you will have multiple consumers for a single event.
 * However, make sure to configure the host event *sleep* mask in coreboot to
 * include MKBP events.  In order to prevent all MKBP events from waking the AP,
 * use CONFIG_MKBP_EVENT_WAKEUP_MASK to filter the events.
 */
#undef CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT

/* MKBP events are sent by using HECI on an ISH */
#undef CONFIG_MKBP_USE_HECI

/* MKBP events are sent by using custom method */
#undef CONFIG_MKBP_USE_CUSTOM

/*
 * If using MKBP to send host events, with this option, we can define the host
 * events that should wake the system in suspend.  Some examples are:
 *
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED)
 *
 * The only things that should be in this mask are:
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_*)
 */
#undef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK

/*
 * Define which MKBP events should wakeup the system in suspend.  Some examples
 * are:
 *
 *    EC_MKBP_EVENT_KEY_MATRIX
 *    EC_MKBP_EVENT_SWITCH
 *
 * The only things that should be in this mask are EC_MKBP_EVENT_*.
 */
#undef CONFIG_MKBP_EVENT_WAKEUP_MASK

/* Support memory protection unit (MPU) */
#undef CONFIG_MPU

/* Do not try hold I/O pins at frozen level during deep sleep */
#undef CONFIG_NO_PINHOLD

/* Support one-wire interface */
#undef CONFIG_ONEWIRE

/* Support One Time Protection structure */
#undef CONFIG_OTP

/*
 * Address to store persistent panic data at. By default, this will be
 * at the end of RAM, and have a size of sizeof(struct panic_data)
 */
#undef CONFIG_PANIC_DATA_BASE
#undef CONFIG_PANIC_DATA_SIZE

/* Support PECI interface to x86 processor */
#undef CONFIG_PECI

/* Common code for PECI interface to x86 processor */
#undef CONFIG_PECI_COMMON

/*
 * Maximum operating temperature in degrees Celcius used on some x86
 * processors. CPU chip temperature is reported relative to this value and
 * is never reported greater than this value. Processor asserts PROCHOT#
 * and starts throttling frequency and voltage at this temp. Operation may
 * become unreliable if temperature exceeds this limit.
 */
#undef CONFIG_PECI_TJMAX

/*****************************************************************************/
/* PMU config */

/*
 * Enable hard-resetting the PMU from the EC.  The implementation is rather
 * hacky; it simply shorts out the 3.3V rail to force the PMIC to panic.  We
 * need this unfortunate hack because it's the only way to reset the I2C engine
 * inside the PMU.
 */
#undef CONFIG_PMU_HARD_RESET

/*
 * Enable this config to make console UART self sufficient (no other
 * initialization required before uart_init(), no interrupts, uart_tx_char()
 * does not exit until character finished transmitting).
 *
 * This is useful during early hardware bringup, each platform needs to
 * implement its own code to support this.
 */
#undef CONFIG_POLLING_UART

/* Define length of history buffer for port80 messages. */
#define CONFIG_PORT80_HISTORY_LEN 128

/*
 * Enable/Disable printing of port80 messages in interrupt context. By default,
 * this is disabled.
 */
#define CONFIG_PORT80_PRINT_IN_INT 0

/* MAX695x 7 segment driver */
#undef CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY

/* Config for power states and port80 message to be displayed on 7 -segment */
#undef CONFIG_SEVEN_SEG_DISPLAY

/* For customer desgin to show port 80 on 7-segment */
#undef CONFIG_CUSTOMER_PORT80

/* For some OS image does not support power button scan code */
#undef CONFIG_IGNORED_BTN_SCANCODE

/* For customer boot from G3 */
#undef CONFIG_CUSTOM_BOOT_G3

/* Compile common code to support power button debouncing */
#undef CONFIG_POWER_BUTTON

/* Configure power button. e.g. BUTTON_FLAG_ACTIVE_HIGH */
#undef CONFIG_POWER_BUTTON_FLAGS

/* Allow the power button to send events while the lid is closed */
#undef CONFIG_POWER_BUTTON_IGNORE_LID

/* Support sending the power button signal to x86 chipsets */
#undef CONFIG_POWER_BUTTON_X86

/* Support sending the power button signal by custom */
#undef CONFIG_POWER_BUTTON_CUSTOM

#if defined(CONFIG_POWER_BUTTON_X86) && defined(CONFIG_POWER_BUTTON_CUSTOM)
#error Only can define one version of power button state machine
#endif

/* Set power button state idle at init. Implemented only for npcx. */
#undef CONFIG_POWER_BUTTON_INIT_IDLE

/* Timeout before power button task gives up starting system */
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT	1

/*
 * Enable delay between DSW_PWROK and PWRBTN assertion.
 * If enabled, DSW_PWROK_TO_PWRBTN_US and get_time_dsw_pwrok must be defined
 * as well.
 */
#undef CONFIG_DELAY_DSW_PWROK_TO_PWRBTN

/*
 * The time in usec required for PMC to be ready to detect power button press.
 * Refer to the timing diagram for G3 to S0 on PDG for details.
 */
#define CONFIG_DSW_PWROK_TO_PWRBTN_US (95 * MSEC)

/*
 * Board provides board_pwrbtn_to_pch function instead of GPIO_PCH_PWRBTN_L
 * as the means for asserting power button signal to PCH.
 */
#undef CONFIG_POWER_BUTTON_TO_PCH_CUSTOM

/* Compile common code for AP power state machine */
#undef CONFIG_POWER_COMMON

/* Enable a task-safe way to control the PP5000 rail. */
#undef CONFIG_POWER_PP5000_CONTROL

/* Support stopping in S5 on shutdown */
#undef CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5

/*
 * Detect power signal interrupt storms, defined as more than
 * CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD occurences of a single
 * power signal interrupt within one second.
 */
#undef CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD

/* Use part of the EC's data EEPROM to hold persistent storage for the AP. */
#undef CONFIG_PSTORE

/* Support S0ix */
#undef CONFIG_POWER_S0IX

/* Support detecting failure to enter a sleep state (S0ix/S3) */
#undef CONFIG_POWER_SLEEP_FAILURE_DETECTION

/*
 * Allow the host to self-report its sleep state, in case there is some delay
 * between the host beginning to enter the sleep state and power signals
 * actually reflecting the new state.
 */
#undef CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/*
 * Implement the '%li' printf format as a *32-bit* integer format,
 * as it might be expected by non-EC code.
 */
#undef CONFIG_PRINTF_LEGACY_LI_FORMAT

/*
 * On x86 systems, define this option if the CPU_PROCHOT signal is active low.
 * This setting also applies to monitoring the PROCHOT input if provided by
 * the board.
 */
#undef CONFIG_CPU_PROCHOT_ACTIVE_LOW

/* Support PS/2 interface */
#undef CONFIG_PS2

/*
 * Define this option to enable programmable voltage detector which will
 * trigger an interrupt when the voltage drops below a threshold specified
 * by the PVD_THRESHOLD which is a chip specific voltage threshold that
 * must be defined in board.h.
 */
#undef CONFIG_PVD

/*****************************************************************************/
/* Support PWM control */
#undef CONFIG_PWM

/* Define clock input to PWM module. */
#undef CONFIG_PWM_INPUT_LFCLK

/*****************************************************************************/
/* Support PWM output to display backlight */
#undef CONFIG_PWM_DISPLIGHT

/*
 * Support keyboard backlight control
 *
 * You need to define board_kblight_init unless CONFIG_PWM_KBLIGHT is used.
 * For example, lm3509 can be registered as a driver in board_kblight_init.
 */
#undef CONFIG_KEYBOARD_BACKLIGHT

/*
 * Support PWM output to keyboard backlight
 *
 * This implies CONFIG_KEYBOARD_BACKLIGHT.
 */
#undef CONFIG_PWM_KBLIGHT

/* Support Real-Time Clock (RTC) */
#undef CONFIG_RTC

/* Size of each RAM bank in chip, default is CONFIG_RAM_SIZE */
#undef CONFIG_RAM_BANK_SIZE

/*
 * Number of RAM banks in chip, default is
 * CONFIG_RAM_SIZE / CONFIG_RAM_BANK_SIZE
 */
#undef CONFIG_RAM_BANKS

/* Base address of RAM for the chip */
#undef CONFIG_RAM_BASE

/*
 * Base address of ROM for the chip. Only used in no physical flash case (
 * !CONFIG_FLASH_PHYSICAL).
 */
#undef CONFIG_ROM_BASE

/*
 * CONFIG_DATA_RAM_SIZE and CONFIG_RAM_SIZE indicate size of all data RAM
 * available on the chip in bytes and size of data RAM available for EC in
 * bytes, respectively.
 * Usually, CONFIG_DATA_RAM_SIZE = CONFIG_RAM_SIZE but some chips need to
 * allocate RAM for the mask ROM. Then CONFIG_DATA_RAM_SIZE > CONFIG_RAM_SIZE.
 *
 * CONFIG_CODE_RAM_SIZE indicates the size of all code RAM available on the chip
 * in bytes.  This is needed when a chip with external storage where stored with
 * code section, or a chip without an internal flash but need to protect its
 * code section by MPU.
 * Usually, CONFIG_CODE_RAM_SIZE = CONFIG_RO_SIZE.  However, some chips may
 * have other value, e.g. mt_scp which doesn't have RO image, and the code RAM
 * size is actually its CONFIG_ROM_SIZE plus a reserved memory space.
 *
 * CONFIG_ROM_SIZE indicates the size of ROM allocated by a linker script.  This
 * is only needed when no physical flash present (!CONFIG_FLASH_PHYSICAL).  The
 * ROM region will place common RO sections, e.g. .text, .rodata, .data LMA etc.
 */
#undef CONFIG_CODE_RAM_SIZE
#undef CONFIG_DATA_RAM_SIZE
#undef CONFIG_RAM_SIZE
#undef CONFIG_ROM_SIZE

/* Support IR357x Link voltage regulator debugging / reprogramming */
#undef CONFIG_REGULATOR_IR357X

/* Support RMA auth challenge-response */
#undef CONFIG_RMA_AUTH

/*
 * Use the p256 curve for RMA challenge-response calculations (x21559 is used
 * by default).
 */
#undef CONFIG_RMA_AUTH_USE_P256

/* Enable hardware Random Number generator support */
#undef CONFIG_RNG

/* Support verifying 2048-bit RSA signature */
#undef CONFIG_RSA

/* Define the RSA key size. */
#undef CONFIG_RSA_KEY_SIZE

/* Use RSA exponent 3 instead of F4 (65537) */
#undef CONFIG_RSA_EXPONENT_3

/*
 * Adjust the compiler optimization flags for the RSA code to get a speed-up
 * at the expense of a small code size delta.
 */
#undef CONFIG_RSA_OPTIMIZED

/*
 * Verify the RW firmware using the RSA signature.
 * (for accessories without software sync)
 */
#undef CONFIG_RWSIG

/*
 * Disable rwsig jump when the reset source is hard pin-reset. This only work
 * for the case where rwsig task is not used.
 */
#undef CONFIG_RWSIG_DONT_CHECK_ON_PIN_RESET

/*
 * When RWSIG verification is performed as a task, time to wait from signature
 * verification to an automatic jump to RW (if AP does not request the wait to
 * be interrupted).
 */
#define CONFIG_RWSIG_JUMP_TIMEOUT (1000 * MSEC)

/*
 * Defines what type of futility signature type should be used.
 * RWSIG should be used for new designs.
 * Old adapters use the USBPD1 futility signature type.
 */
#undef CONFIG_RWSIG_TYPE_RWSIG
#undef CONFIG_RWSIG_TYPE_USBPD1

/*
 * By default the pubkey and sig are put at the end of the first and second
 * half of the total flash, and take up the minimum space possible. You can
 * override those defaults with these.
 */
#undef CONFIG_RO_PUBKEY_ADDR
#undef CONFIG_RO_PUBKEY_SIZE
#undef CONFIG_RW_SIG_ADDR
#undef CONFIG_RW_SIG_SIZE

/* Size of the serial number if needed */
#undef CONFIG_SERIALNO_LEN

/* Support programmable Mac address field. */
#undef CONFIG_MAC_ADDR

/* Size of the MAC address field if needed. */
#undef CONFIG_MAC_ADDR_LEN

/****************************************************************************/
/* Shared objects library. */

/* Support shared objects library between RO and RW. */
#undef CONFIG_SHAREDLIB

/* Size of shared objects library. */
#undef CONFIG_SHAREDLIB_SIZE

/* Program memory offset of shared objects library. */
#undef CONFIG_SHAREDLIB_MEM_OFF

/* Storage  offset of sharedobjects library. */
#undef CONFIG_SHAREDLIB_STORAGE_OFF

/*
 * If defined, the hash module will save its last computed hash when jumping
 * between EC images.
 */
#undef CONFIG_SAVE_VBOOT_HASH

/* Allow the board to use a GPIO for the SCI# signal. */
#undef CONFIG_SCI_GPIO

/* Support computing of other hash sizes (without the VBOOT code) */
#undef CONFIG_SHA256

/* Unroll some loops in SHA256_transform for better performance. */
#undef CONFIG_SHA256_UNROLLED

/* Emulate the CLZ (Count Leading Zeros) in software for CPU lacking support */
#undef CONFIG_SOFTWARE_CLZ

/* Emulate the CLZ (Count Trailing Zeros) in software for CPU lacking support */
#undef CONFIG_SOFTWARE_CTZ

/* Support smbus interface */
/*
 * Deprecated in
 * https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/1704279
 *
 * It hasn't been used in over 2 years
 * https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/452459/
 * and was only used by one board (pyro).
 *
 * I2C and SMBus are compatible at the physical layer. The data transfer
 * paradigm is different. Some of our batteries are using SMbus style
 * transfers now, they are just using i2cxfer directly to accomplish it.
 *
 * I doubt the SMBus code will get revived, but we do have it in revision
 * history if we ever need it.
 */
/* #undef CONFIG_SMBUS */

/* Support SPI interfaces */
#undef CONFIG_SPI

/* Support deprecated SPI protocol version 2. */
#undef CONFIG_SPI_PROTOCOL_V2

/* Define the SPI port to use to access SPI accelerometer */
#undef CONFIG_SPI_ACCEL_PORT

/* Support SPI flash */
#undef CONFIG_SPI_FLASH

/* Support SPI flash protection register translation */
#undef CONFIG_SPI_FLASH_REGS

/* Define the SPI port to use to access the flash */
#undef CONFIG_SPI_FLASH_PORT

/* Select any of the following SPI flash configs that your board uses. */
#undef CONFIG_SPI_FLASH_GD25LQ40
#undef CONFIG_SPI_FLASH_GD25Q41B
#undef CONFIG_SPI_FLASH_W25Q128
#undef CONFIG_SPI_FLASH_W25Q40
#undef CONFIG_SPI_FLASH_W25Q64
#undef CONFIG_SPI_FLASH_W25Q80
#undef CONFIG_SPI_FLASH_W25X40

/* SPI flash part supports SR2 register */
#undef CONFIG_SPI_FLASH_HAS_SR2

/* Define the SPI port to use to access the fingerprint sensor */
#undef CONFIG_SPI_FP_PORT

/* Support JEDEC SFDP based Serial NOR flash */
#undef CONFIG_SPI_NOR

/* Enable SPI_NOR debugging providing additional console output while
 * initializing Serial NOR Flash devices including SFDP discovery. */
#undef CONFIG_SPI_NOR_DEBUG

/* Maximum Serial NOR flash command size, in Bytes */
#undef CONFIG_SPI_NOR_MAX_MESSAGE_SIZE

/* Maximum Serial NOR flash read size, in Bytes */
#undef CONFIG_SPI_NOR_MAX_READ_SIZE

/* Maximum Serial NOR flash write size, in Bytes. Note this must be a power of
 * two. */
#undef CONFIG_SPI_NOR_MAX_WRITE_SIZE

/* If defined will enable block (64KiB) erase operations. */
#undef CONFIG_SPI_NOR_BLOCK_ERASE

/* If defined will read the sector/block to be erased first and only initiate
 * the erase operation if not already in an erased state. The read operation
 * (performed in CONFIG_SPI_NOR_MAX_READ_SIZE chunks) is aborted early if a
 * non "0xff" byte is encountered.
 * !! Make sure there is enough stack space to host a
 * !! CONFIG_SPI_NOR_MAX_READ_SIZE sized buffer before enabling.
 */
#undef CONFIG_SPI_NOR_SMART_ERASE

/* SPI master feature */
#undef CONFIG_SPI_MASTER

/* SPI master halfduplex/3-wire mode */
#undef CONFIG_SPI_HALFDUPLEX

/* Support STM32 SPI1 as master. */
#undef CONFIG_STM32_SPI1_MASTER

/* SPI master configure gpios on init */
#undef CONFIG_SPI_MASTER_CONFIGURE_GPIOS

/* Support SPI masters without GPIO-specified Chip Selects, instead rely on the
 * SPI master port's hardwired CS pin. */
#undef CONFIG_SPI_MASTER_NO_CS_GPIOS

/* Support MCHP MEC family GP-SPI master(s)
 * Define to 0x01 for GPSPI0 only.
 * Define to 0x02 for GPSPI1 only.
 * Define to 0x03 for both controllers.
 */
#undef CONFIG_MCHP_GPSPI

/* Default stack size to use for tasks, in bytes */
#undef CONFIG_STACK_SIZE

/* Use 32-bit timer for clock source on stm32. */
#undef CONFIG_STM_HWTIMER32

/* Compile charger detect for STM32 */
#undef CONFIG_STM32_CHARGER_DETECT

/* Fake hibernate mode */
#undef CONFIG_STM32L_FAKE_HIBERNATE

/*
 * Compile common code to handle simple switch inputs such as the recovery
 * button input from the servo debug interface.
 */
#undef CONFIG_SWITCH

/* Support dedicated recovery signal from servo board */
#undef CONFIG_SWITCH_DEDICATED_RECOVERY

/*
 * System should remain unlocked even if write protect is enabled.
 *
 * NOTE: This should ONLY be defined during bringup, and should never be
 * defined on a shipping / released platform.
 *
 * When defined, CBI allows ectool to reprogram all the fields. Once undefined,
 * it refuses to change certain fields. (e.g. board version, OEM ID)
 *
 * Also, this will enable PD in RO for TCPMv2.
 */
#undef CONFIG_SYSTEM_UNLOCKED

/*
 * Device can be a tablet as well as a clamshell.
 */
#undef CONFIG_TABLET_MODE

/*
 * Add a virtual switch to indicate when we are in tablet mode.
 */
#undef CONFIG_TABLET_MODE_SWITCH

/*
 * Config to identify what devices use GMR sensor to detect tablet mode. If a
 * board selects this config, it also needs to provide GMR_TABLET_MODE_GPIO_L
 * and direct its interrupt to gmr_tablet_switch_isr.
 */
#undef CONFIG_GMR_TABLET_MODE

/*
 * Board provides board_sensor_at_360 method instead of GMR_TABLET_MODE_GPIO_L
 * as the means for determining the state of the flipped-360-degree mode.
 */
#undef CONFIG_GMR_TABLET_MODE_CUSTOM

/*
 * Add a virtual switch to indicate when detachable device has
 * base attached.
 */
#undef CONFIG_BASE_ATTACHED_SWITCH

/*
 * Microchip Trace FIFO Debug Port
 */
#undef CONFIG_MCHP_TFDP

/*
 * Mirochip EMI region 1 enable
 */
#undef CONFIG_EMI_REGION1

/*****************************************************************************/
/* Task config */

/*
 * List of enabled tasks in ascending priority order. This is normally
 * defined in each board's ec.tasklist file.
 *
 * For each task, use the macro TASK_ALWAYS(n, r, d, s) for base tasks and
 * TASK_NOTEST(n, r, d, s) for tasks that can be excluded in test binaries,
 * where:
 * 'n' is the name of the task
 * 'r' is the main routine of the task
 * 'd' is an opaque parameter passed to the routine at startup
 * 's' is the stack size in bytes; must be a multiple of 8
 *
 * Some cores use TASK_ALWAYS(n, r, d, s, f), where:
 * 'f' is the bit flags for the platform specific information
 *    - MIA_TASK_FLAG_USE_FPU : bit 0, task uses FPU H/W
 *
 * For USB PD tasks, IDs must be in consecutive order and correspond to
 * the port which they are for. See TASK_ID_TO_PD_PORT() macro.
 */
#undef CONFIG_TASK_LIST

/*
 * List of test tasks.  Same format as CONFIG_TASK_LIST, but used to define
 * additional tasks for a unit test.  Normally defined in
 * test/{testname}.tasklist.
 */
#undef CONFIG_TEST_TASK_LIST

/*
 * List of tasks used by CTS
 *
 * cts.tasklist contains tasks run only for CTS. These tasks are added to the
 * tasks registered in ec.tasklist with higher priority.
 *
 * If a CTS suite does not define its own cts.tasklist, the common list is used
 * (i.e. cts/cts.tasklist).
 */
#undef CONFIG_CTS_TASK_LIST

/*
 * List of tasks that support reset. Tasks listed here must also be included in
 * CONFIG_TASK_LIST.
 *
 * For each task, use macro ENABLE_RESET(n) to enable resets. The parameter n
 * must match the value passed to TASK_{ALWAYS,NOTEST} in CONFIG_TASK_LIST.
 *
 * Tasks that enable resets *must* call task_reset_cleanup() once at the
 * beginning of their main function, and perform task-specific cleanup if
 * necessary.
 *
 * By default, tasks can be reset at any time. To change this behavior, call
 * task_disable_resets() immediately after task_reset_cleanup(), and then enable
 * resets where appropriate.
 *
 * Tasks that predominantly have resets disabled are expected to periodically
 * enable resets, and should always ensure to do so before waiting for long
 * periods (eg when waiting for an event to process).
 */
#undef CONFIG_TASK_RESET_LIST

/*
 * Enable task profiling.
 *
 * Boards may #undef this to reduce image size and RAM usage.
 */
#define CONFIG_TASK_PROFILING

/*****************************************************************************/
/* Mock config */

/*
 * List of mock implementations to pull into the build.
 *
 * This should contain a flat list of MOCK(the-mock-name) elements.
 *
 * This is defined in the following two files:
 * test/{testname}.mocklist
 * fuzz/{fuzzname}.mocklist
 */
#undef CONFIG_TEST_MOCK_LIST

/*****************************************************************************/
/* Temperature sensor config */

/* Compile common code for temperature sensor support */
#undef CONFIG_TEMP_SENSOR

/* Support particular temperature sensor chips */
#undef CONFIG_TEMP_SENSOR_ADT7481	/* ADT 7481 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_BD99992GW	/* BD99992GW PMIC, on I2C bus */
#undef CONFIG_TEMP_SENSOR_EC_ADC        /* Thermistors on EC's own ADC */
#undef CONFIG_TEMP_SENSOR_G753		/* G753 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_G781		/* G781 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_G782		/* G782 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_OTI502	/* OTI502 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_SB_TSI	/* SB_TSI sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP006	/* TI TMP006 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP411	/* TI TMP411 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP432	/* TI TMP432 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP468	/* TI TMP468 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_F75303	/* Fintek  F75303 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_AMD_R19ME4070	/* AMD_R19ME4070 sensor, on I2C bus */

/* Compile common code for thermistor support */
#undef CONFIG_THERMISTOR

/* Support particular thermistors */
#undef CONFIG_THERMISTOR_NCP15WB	/* NCP15WB thermistor */

/*
 * If defined, image includes lookup tables and helper functions that convert
 * thermistor ADC readings into degrees K based off of various circuit
 * configurations.
 */
#undef CONFIG_STEINHART_HART_3V0_22K6_47K_4050B
#undef CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#undef CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#undef CONFIG_STEINHART_HART_6V0_51K1_47K_4050B
#undef CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/*
 * If defined, active-high GPIO which indicates temperature sensor chips are
 * powered.  If not defined, temperature sensors are assumed to be always
 * powered.
 */
#undef CONFIG_TEMP_SENSOR_POWER_GPIO

/* Compile common code for throttling the CPU based on the temp sensors */
#undef CONFIG_THROTTLE_AP

/*
 * Throttle the CPU when battery discharge current is too high. When
 * this feature is enabled, BAT_MAX_DISCHG_CURRENT must be defined in board.h.
 */
#undef CONFIG_THROTTLE_AP_ON_BAT_DISCHG_CURRENT

/*
 * Throttle the CPU when battery voltage drops below a defined threshold
 * where the board still boots but some components don't function perfectly.
 * When this feature is enabled, BAT_LOW_VOLTAGE_THRESH must be defined in
 * board.h.
 */
#undef CONFIG_THROTTLE_AP_ON_BAT_VOLTAGE

/*
 * If defined, dptf is enabled to manage thermals.
 *
 * NOTE: This doesn't mean that thermal control is completely taken care by
 * DPTF. We have some hybrid solutions where the EC still manages the fans.
 */
#undef CONFIG_DPTF

/*
 * If defined, this indicates to the motion lid driver that the board does not
 * have any GMR sensor and hence DPTF profile selection is required to be done
 * based on lid angle.
 */
#undef CONFIG_DPTF_MOTION_LID_NO_GMR_SENSOR

/*
 * If defined, device supports multiple DPTF profiles depending upon device mode
 * e.g. clamshell v/s 360-degree flipped mode or base detached v/s attached
 * mode.
 *
 * This config can be used by any driver that does lid angle calculation or base
 * state detection to determine if different profile numbers need to be
 * indicated to the host.
 */
#undef CONFIG_DPTF_MULTI_PROFILE

/*****************************************************************************/
/* Touchpad config */

/* Enable touchpad. (You must pick a driver from the options below.) */
#undef CONFIG_TOUCHPAD

/* Enable Elan driver */
#undef CONFIG_TOUCHPAD_ELAN

/* Enable Goodix GT7288 driver */
#undef CONFIG_TOUCHPAD_GT7288

/* Enable ST driver */
#undef CONFIG_TOUCHPAD_ST

/* Set I2C port and address (7-bit) */
#undef CONFIG_TOUCHPAD_I2C_PORT
#undef CONFIG_TOUCHPAD_I2C_ADDR_FLAGS

/*
 * Enable touchpad FW update over USB update protocol, and define touchpad
 * virtual address and size.
 */
#undef CONFIG_TOUCHPAD_VIRTUAL_OFF
#undef CONFIG_TOUCHPAD_VIRTUAL_SIZE

/*
 * Include hashes of the touchpad FW in the EC image, passed as TOUCHPAD_FW
 * parameter to make command.
 */
#undef CONFIG_TOUCHPAD_HASH_FW

/*****************************************************************************/
/* USART stream config */
#undef CONFIG_STREAM_USART

/*
 * Each USART stream can be individually enabled and accessible using the
 * stream interface provided in the usart_config struct.
 */
#undef CONFIG_STREAM_USART1
#undef CONFIG_STREAM_USART2
#undef CONFIG_STREAM_USART3
#undef CONFIG_STREAM_USART4

/*****************************************************************************/
/* USB stream config */
#undef CONFIG_STREAM_USB

/*****************************************************************************/
/* UART HOST COMMAND config */

/* Includes USART as host command interface */
#undef CONFIG_USART_HOST_COMMAND

/* Pointer to USART HW config of physical instance */
#undef CONFIG_UART_HOST_COMMAND_HW

/*
 * USART baudrate for host command interface.
 * Typically configured at 3000000 to handle use cases
 * like firmware download and big packets in a reasonable time.
 */
#undef CONFIG_UART_HOST_COMMAND_BAUD_RATE

/*****************************************************************************/
/* UART config */

/* Baud rate for UARTs */
#define CONFIG_UART_BAUD_RATE 115200

/* UART index (number) for EC console */
#undef CONFIG_UART_CONSOLE

/* UART index (number) for host UART, if present */
#undef CONFIG_UART_HOST

/* Use uart_input_filter() to filter UART input. See prototype in uart.h */
#undef CONFIG_UART_INPUT_FILTER

/*
 * Allow switching the EC console UART to an alternate pad. This must be
 * used for short transactions only, and EC is only able to receive data on
 * that alternate pad after it has been explicitly switched.
 */
#undef CONFIG_UART_PAD_SWITCH

/**
 * This will only be used for Kukui and cortex-m0. Preserve EC reset logs and
 * console logs on SRAM so that the logs will be preserved after EC shutting
 * down or sysjumped. It will keep the contents across EC resets, so we have
 * more information about system states. The contents on SRAM will be cleared
 * when checksum or validity check fails.
 */
#undef CONFIG_PRESERVE_LOGS

/*
 * UART receive buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.  Must be larger than
 * CONFIG_CONSOLE_INPUT_LINE_SIZE to copy and paste scripts.
 */
#define CONFIG_UART_RX_BUF_SIZE 128

/* Use DMA for UART input */
#undef CONFIG_UART_RX_DMA

/*
 * On some platforms, UART receive DMA can't trigger an interrupt when a single
 * character is received.  Those platforms poll for characters every HOOK_TICK.
 * When a character is received, make this many additional checks between then
 * and the next HOOK_TICK, to increase responsiveness of the console to input.
 */
#define CONFIG_UART_RX_DMA_RECHECKS 5

/*
 * UART transmit buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.
 */
#define CONFIG_UART_TX_BUF_SIZE 512

/* Use DMA for UART output */
#undef CONFIG_UART_TX_DMA

/* The DMA channel for UART.  If not defined, default to UART1. */
#undef CONFIG_UART_TX_DMA_CH
#undef CONFIG_UART_RX_DMA_CH

/* The DMA peripheral request signal for UART TX. STM32 only. */
#undef CONFIG_UART_TX_DMA_PH

/* The DMA channel mapping config for stm32f4. */
#undef CONFIG_UART_TX_REQ_CH
#undef CONFIG_UART_RX_REQ_CH

/* Driver of LN9310 switchcap */
#undef CONFIG_LN9310

/*****************************************************************************/
/* USB PD config */

/* Config is enabled, if PD interrupt tasks are used. */
#undef CONFIG_HAS_TASK_PD_INT

/*
 * Enables USB Power Delivery
 *
 * When this config option is enabled, one of the following must be enabled:
 *	CONFIG_USB_PD_TCPMV1 - legacy power delivery state machine
 *	CONFIG_USB_PD_TCPMV2 - current power delivery state machine
 */
#undef CONFIG_USB_POWER_DELIVERY

/*
 * Enables the Legacy power delivery state machine.
 * NOTE: Should not be used for new designs.
 */
#undef CONFIG_USB_PD_TCPMV1

/*
 * Enables PD protocol state names in the TPCMv1 console output.
 * Disable to save ~900 bytes in flash space.
 */
#define CONFIG_USB_PD_TCPMV1_DEBUG

/*
 * Enables Version 2 of the Power Delivery state machine
 *
 * Along with CONFIG_USB_PD_TCPMV2, you must ensure a device type is also
 * enabled otherwise an error will be emitted.
 */
#undef CONFIG_USB_PD_TCPMV2

/*
 * Device Types for TCPMv2.
 *
 * Exactly one must be defined when CONFIG_USB_PD_TCPMV2 is defined.
 *
 * VPD - Vconn Powered Device
 * CTVPD - Charge Through Vconn Powered Device
 * DRP_ACC_TRYSRC - Dual Role Port, Audio Accessory, and Try.SRC Device
 */
#undef CONFIG_USB_VPD
#undef CONFIG_USB_CTVPD
#undef CONFIG_USB_DRP_ACC_TRYSRC

/*
 * TCPMv2 statemachine layers
 *
 * All layers are defined by default. To opt-out, you must undef in your board.
 * Also these defines don't take effect unless CONFIG_USB_PD_TCPMV2 is also
 * defined.
 *
 * TYPEC_SM - Type-C deals with CC lines voltage level connections
 * PRL_SM - Protocol handles flow and chunking TX and RX messages
 * PE - Policy Engine handles PD communication flow
 */
#define CONFIG_USB_TYPEC_SM
#define CONFIG_USB_PRL_SM
#define CONFIG_USB_PE_SM

/* Enables PD Console commands */
#define CONFIG_USB_PD_CONSOLE_CMD

/* Enables PD Host commands */
#define CONFIG_USB_PD_HOST_CMD

/* Support for USB PD alternate mode */
#undef CONFIG_USB_PD_ALT_MODE

/* Support for USB PD alternate mode of Downward Facing Port */
#undef CONFIG_USB_PD_ALT_MODE_DFP

/* HPD is sent to the GPU from the EC via a GPIO */
#undef CONFIG_USB_PD_DP_HPD_GPIO

/*
 * HPD is sent to the GPU from the EC via a GPIO, and the HPD GPIO level has
 * to be handled separately.
 */
#undef CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM

/* Check if max voltage request is allowed before each request */
#undef CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED

/* Default state of PD communication disabled flag */
#undef CONFIG_USB_PD_COMM_DISABLED

/*
 * Define this if a board needs custom SNK and/or SRC PDOs.
 *
 * The default SRC PDO is a fixed 5V/1.5A with PDO_FIXED_FLAGS indicating
 * Dual-Role power, USB Communication Capable, and Dual-Role data.
 *
 * The default SNK PDOs are:
 * - Fixed 5V/500mA with the same PDO_FIXED_FLAGS
 * - Variable (non-battery) min 4.75V, max PD_MAX_VOLTAGE_MV, operational
 *   current 3A
 * - Battery min 4.75V, max PD_MAX_VOLTAGE_MV, operational power 15W
 */
#undef CONFIG_USB_PD_CUSTOM_PDO

/*
 * Do not enable PD communication in RO as a security measure.
 * We don't want to allow communication to outside world until
 * we jump to RW. This can by overridden with the removal of
 * the write protect screw to allow for easier testing.
 *
 * Note: this is assumed for TCPMv2. See also CONFIG_BRINGUP for enabling PD in
 * RO.
 */
#undef CONFIG_USB_PD_COMM_LOCKED

/* Default USB data role when a USB PD debug accessory is seen */
#define CONFIG_USB_PD_DEBUG_DR PD_ROLE_DFP

/*
 * Define to have a fixed PD Task debug level.
 * Undef to allow runtime change via console command.
 */
#undef CONFIG_USB_PD_DEBUG_LEVEL

/*
 * Define if this board can enable VBUS discharge (eg. through a GPIO-controlled
 * discharge circuit, or through port controller registers) to discharge VBUS
 * rapidly on disconnect. Will be defined automatically when one of the below
 * options is defined.
 */
#undef CONFIG_USB_PD_DISCHARGE

/* Define if discharge circuit is EC GPIO-controlled. */
#undef CONFIG_USB_PD_DISCHARGE_GPIO

/* Define if discharge circuit is using PD discharge registers on TCPC. */
#undef CONFIG_USB_PD_DISCHARGE_TCPC

/* Define if discharge circuit is using PD discharge registers on PPC. */
#undef CONFIG_USB_PD_DISCHARGE_PPC

/* Define if this board can act as a dual-role PD port (source and sink) */
#undef CONFIG_USB_PD_DUAL_ROLE

/* Define if this board can used TCPC-controlled DRP toggle */
#undef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE

/* Define to reduces VBUS droop caused by inrush current during charging */
#undef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT

/* Initial DRP / toggle policy */
#define CONFIG_USB_PD_INITIAL_DRP_STATE PD_DRP_TOGGLE_OFF

/*
 * Define if VBUS source GPIOs (GPIO_USB_C*_5V_EN) are active-low (and named
 * (..._L) rather than default active-high.
 */
#undef CONFIG_USB_PD_5V_EN_ACTIVE_LOW

/* Ask charger if VBUS is enabled on a source port, instead of using GPIO */
#undef CONFIG_USB_PD_5V_CHARGER_CTRL

/*
 * If defined, use a custom function to determine if VBUS is enabled on a
 * source port. The custom function is board_is_sourcing_vbus(port).
 */
#undef CONFIG_USB_PD_5V_EN_CUSTOM

/* Dynamic USB PD source capability */
#undef CONFIG_USB_PD_DYNAMIC_SRC_CAP

/* Support USB PD flash. */
#undef CONFIG_USB_PD_FLASH

/* Check whether PD is the sole power source before flash erase operation */
#undef CONFIG_USB_PD_FLASH_ERASE_CHECK

/* Define if this board, operating as a sink, can give power back to a source */
#undef CONFIG_USB_PD_GIVE_BACK

/*
 * PD Rev2.0 functionality is enabled by default. Defining this macro
 * enables PD Rev3.0 functionality.
 */
#undef CONFIG_USB_PD_REV30

/* Defined automatically based on on maximum PD revision supported. */
#undef CONFIG_PD_RETRY_COUNT

/*
 * Support USB PD 3.0 Extended Messages. This will only take effect if
 * CONFIG_USB_PD_REV30 is also enabled. Note that Chromebooks disabling this
 * config item are non-compliant with PD 3.0, because they have batteries but do
 * not support Get_Battery_Cap or Get_Battery_Status.
 */
#define CONFIG_USB_PD_EXTENDED_MESSAGES

/* Major and Minor ChromeOS specific PD device Hardware IDs. */
#undef CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR
#undef CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR

/* HW & SW version for alternate mode discover identity response (4bits each) */
#undef CONFIG_USB_PD_IDENTITY_HW_VERS
#undef CONFIG_USB_PD_IDENTITY_SW_VERS

/* USB PD MCU slave address for host commands */
#define CONFIG_USB_PD_I2C_SLAVE_ADDR_FLAGS 0x1E

/* Define if using internal comparator for PD receive */
#undef CONFIG_USB_PD_INTERNAL_COMP

/* Record main PD events in a circular buffer */
#undef CONFIG_USB_PD_LOGGING

/* The size in bytes of the FIFO used for event logging */
#define CONFIG_EVENT_LOG_SIZE 512

/* Save power by waking up on VBUS rather than polling CC */
#define CONFIG_USB_PD_LOW_POWER

/* Allow chip to go into low power idle even when a PD device is attached */
#undef CONFIG_USB_PD_LOW_POWER_IDLE_WHEN_CONNECTED

/* Number of USB PD ports */
#undef CONFIG_USB_PD_PORT_MAX_COUNT

/*
 * Number of ITE USB PD active ports
 * NOTE: The active port usage should follow the order of ITE TCPC port index.
 */
#undef CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT

/* Simple DFP, such as power adapter, will not send discovery VDM on connect */
#undef CONFIG_USB_PD_SIMPLE_DFP

/* Use comparator module for PD RX interrupt */
#define CONFIG_USB_PD_RX_COMP_IRQ

/* Use TCPC module (type-C port controller) */
#undef CONFIG_USB_PD_TCPC

/* Board provides specific TCPC init function */
#undef CONFIG_USB_PD_TCPC_BOARD_INIT

/* Enable TCPC to enter low power mode */
#undef CONFIG_USB_PD_TCPC_LOW_POWER

/*
 * Default debounce when exiting low-power mode before checking CC status.
 * Some TCPCs need additional time following a VBUS change to internally
 * debounce the CC line status and updating the CC_STATUS register.
 */
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE	(25*MSEC)

/* Define EC and TCPC modules are in one integrated chip */
#undef CONFIG_USB_PD_TCPC_ON_CHIP

/* Enable the encoding of msg SOP* in bits 31-28 of 32-bit msg header type */
#undef CONFIG_USB_PD_DECODE_SOP

/*
 * The USB4 specification defines compatibility support for USB4 products to
 * interact with existing Thunderbolt 3 products. Enable this config to enter
 * into Thunderbolt-compatible mode between two port partners.
 */
#undef CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enable to enter into USB4 mode between two port partners */
#undef CONFIG_USB_PD_USB4

/* Enable if port is cable of operating as an USB4 device */
#undef CONFIG_USB_PD_USB4_DRD

/* Enable if port is cable of operating as an USB3.2 device */
#undef CONFIG_USB_PD_USB32_DRD

/* Enable if the board is Thunderbolt Gen 3 capable */
#undef CONFIG_USB_PD_TBT_GEN3_CAPABLE

/* Enable PCIE tunneling if Thunderbolt-Compatible mode is enabled*/
#undef CONFIG_USB_PD_PCIE_TUNNELING

/*
 * The following two macros are ASCII text strings that matches what appears
 * in the USB-IF Product Registration form for this device. These macros are
 * used during VIF generation and they form the product name in the
 * USB Integrator’s List.
 */
#undef CONFIG_USB_PD_MODEL_PART_NUMBER
#undef CONFIG_USB_PD_PRODUCT_REVISION

/*
 * Should be defined if the device is a TypeC Alt Mode Adapter. This macro
 * is used during VIF generation.
 */
#undef CONFIG_USB_ALT_MODE_ADAPTER

/*
 * A text string, provided by the USB-IF. This macro is used during VIF
 * generation.
 */
#undef CONFIG_USB_PD_TID

/*
 * An ASCII text string that must correspond with the port label given on the
 * device picture submitted to USB-IF by the Vendor along with the VIF. This
 * macro is used during VIF generation.
 */
#undef CONFIG_USB_PD_PORT_LABEL

/*
 * Define if Get_Manufacturer_Info request PD message is supported.
 * Used during VIF generation.
 */
#undef CONFIG_USB_PD_MANUFACTURER_INFO

/*
 * Define if both Security_Request and Security_Response PD messages are
 * supported. Used during VIF generation.
 */
#undef CONFIG_USB_PD_SECURITY_MSGS

/*
 * The number of non-removable batteries in the device. Used duing VIF
 * generation.
 */
#undef CONFIG_NUM_FIXED_BATTERIES

/*
 * Track VBUS level in TCPC module. This will only be needed if we're acting
 * as an external TCPC.
 */
#undef CONFIG_USB_PD_TCPC_TRACK_VBUS

/* Enable runtime config the TCPC */
#undef CONFIG_USB_PD_TCPC_RUNTIME_CONFIG

/*
 * Choose one of the following TCPMs (type-C port manager) to manage TCPC. The
 * TCPM stub is used to make direct function calls to TCPC when TCPC is on
 * the same MCU. The TCPCI TCPM uses the standard TCPCI i2c interface to TCPC.
 */
#undef CONFIG_USB_PD_TCPM_STUB
#undef CONFIG_USB_PD_TCPM_TCPCI
#undef CONFIG_USB_PD_TCPM_FUSB302
#undef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#undef CONFIG_USB_PD_TCPM_ANX3429
#undef CONFIG_USB_PD_TCPM_ANX740X
#undef CONFIG_USB_PD_TCPM_ANX741X
#undef CONFIG_USB_PD_TCPM_ANX7447
#undef CONFIG_USB_PD_TCPM_ANX7688
#undef CONFIG_USB_PD_TCPM_NCT38XX
#undef CONFIG_USB_PD_TCPM_MT6370
#undef CONFIG_USB_PD_TCPM_TUSB422
#undef CONFIG_USB_PD_TCPM_RAA489000
#undef CONFIG_USB_PD_TCPM_RT1715
#undef CONFIG_USB_PD_TCPM_FUSB307
#undef CONFIG_USB_PD_TCPM_STM32GX

/* PS8XXX series are all supported by a single driver with a build time config
 * listed below (CONFIG_USB_PD_TCPM_PS*) defined to enable the specific product.
 *
 * If a board with the same EC FW is expected to support multiple products here
 * then CONFIG_USB_PD_TCPM_MULTI_PS8XXX MUST be defined then we can enable more
 * than one product config to support them in the runtime. In this case, board
 * is responsible to override function of board_get_ps8xxx_product_id in order
 * to provide the product id per port.
 */
#undef CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#undef CONFIG_USB_PD_TCPM_PS8751
#undef CONFIG_USB_PD_TCPM_PS8755
#undef CONFIG_USB_PD_TCPM_PS8705
#undef CONFIG_USB_PD_TCPM_PS8805
#undef CONFIG_USB_PD_TCPM_PS8815

/*
 * Defined automatically by chip and depends on chip. This guards the onboard
 * TCPM driver, but CONFIG_USB_PD_TCPM_ITE_ON_CHIP needs to be defined in
 * board.h for either of these driver to actually be included in the final
 * image.
 */
#undef CONFIG_USB_PD_TCPM_DRIVER_IT83XX
#undef CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2

/*
 * Type-C retimer drivers to be used.
 */
#undef CONFIG_USBC_RETIMER_INTEL_BB
#undef CONFIG_USBC_RETIMER_NB7V904M
#undef CONFIG_USBC_RETIMER_PI3DPX1207
#undef CONFIG_USBC_RETIMER_PI3HDX1204
#undef CONFIG_USBC_RETIMER_PS8802
#undef CONFIG_USBC_RETIMER_PS8818
#undef CONFIG_USBC_RETIMER_TUSB544

/* Allow run-time configuration of the Burnside Bridge driver structure */
#undef CONFIG_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG

/*
 * Adds an EC console command to erase the ANX7447 OCM flash.
 * Note: this is intended to be a temporary option and
 * won't be needed when ANX7447 are put on boards with OCM already erased
 */
#undef CONFIG_USB_PD_TCPM_ANX7447_OCM_ERASE_COMMAND

/*
 * Use this config option to enable and internal pullup resistor on the AUX_N
 * and internal pulldown resistor on the AUX_P line. Only use this config
 * option if there are no external pu/pd resistors on these signals. This
 * configuration should be used to avoid noise issues on the DDI1_AUX_N &
 * DDI1_AUX_P signals (b/122873171)
 */
#undef CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD

/*
 * Use this to override the TCPCI Device ID value to be 0x0002 for
 * chip rev A1. Early A1 firmware misreports the DID as 0x0001.
 */
#undef CONFIG_USB_PD_TCPM_PS8815_FORCE_DID

/*
 * Use this option if the TCPC port controller supports the optional register
 * 18h CONFIG_STANDARD_OUTPUT to steer the high-speed muxes.
 */
#undef CONFIG_USB_PD_TCPM_MUX

/*
 * The TCPM must know whether VBUS is present in order to make proper state
 * transitions. In addition, charge_manager must know about VBUS presence in
 * order to make charging decisions. VBUS state can be determined by various
 * methods:
 * - Some TCPCs can detect and report the presence of VBUS.
 * - In some configurations, charger ICs can report the presence of VBUS.
 * - On some boards, dedicated VBUS interrupt pins are available.
 * - Some power path controllers (PPC) can report the presence of VBUS.
 *
 * Exactly one of these should be defined for all boards that run the PD
 * state machine.
 */
#undef CONFIG_USB_PD_VBUS_DETECT_TCPC
#undef CONFIG_USB_PD_VBUS_DETECT_CHARGER
#undef CONFIG_USB_PD_VBUS_DETECT_GPIO
#undef CONFIG_USB_PD_VBUS_DETECT_PPC
#undef CONFIG_USB_PD_VBUS_DETECT_NONE

/* Define if the there is a separate ADC channel for each USB-C Vbus voltage */
#undef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT

/* Define if the there is no hardware to measure Vbus voltage */
#undef CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT

/* Define if charger on the board supports VBUS measurement */
#undef CONFIG_USB_PD_VBUS_MEASURE_CHARGER

/* Define if tcpc on the board supports VBUS measurement */
#undef CONFIG_USB_PD_VBUS_MEASURE_TCPC

/*
 * Define if system use stand alone pd solution.
 * For stand alone solution, PD control VBUS itself, and will notify ec.
 */
#undef CONFIG_USB_PD_VBUS_CTRL_BY_PD_CHIP

/* Define the type-c port controller I2C base address. */
#define CONFIG_TCPC_I2C_BASE_ADDR_FLAGS 0x4E

/* Use this option to enable Try.SRC mode for Dual Role devices */
#undef CONFIG_USB_PD_TRY_SRC

/* Set the default minimum battery percentage for Try.Src to be enabled */
#define CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC 5

/*
 * Set the minimum battery percentage to allow a PD port to send resets as a
 * sink (and risk a hard reset, losing Vbus).  Note this may cause a high-power
 * charger to appear as only a low-power 15W charger until a reset is sent to
 * re-start PD negotiation.
 */
#undef CONFIG_USB_PD_RESET_MIN_BATT_SOC

/* Alternative configuration keeping only the TX part of PHY */
#undef CONFIG_USB_PD_TX_PHY_ONLY

/* Use DAC as reference for comparator at 850mV. */
#undef CONFIG_PD_USE_DAC_AS_REF

/*
 * Request for a PDO which voltage is closest to PD_PREFER_MV for sink.
 * This config in theory could achieve better charging efficiency.  Note this
 * may not always pick the PD_PREFER_MV if available (if the PDO's
 * power not sufficient for the system), it will pick second closest PDO until
 * the system desired power is low enough to be charged by the designed PDO.
 *
 * If defined, must also define `struct pd_pref_config_t pd_pref_config`.
 */
#undef CONFIG_USB_PD_PREFER_MV

/*
 * The Fast Role Swap trigger can be implemented in either the TCPC or PPC
 * driver. If either CONFIG_USB_PD_FRS_TCPC or CONFIG_USB_PD_FRS_PPC is set,
 * CONFIG_USB_FRS will be set automatically to enable the protocol-side of FRS.
 */
#undef CONFIG_USB_PD_FRS_TCPC
#undef CONFIG_USB_PD_FRS_PPC
#undef CONFIG_USB_PD_FRS

/*
 * USB Product ID. Each platform (e.g. baseboard set) should have a single
 * VID/PID combination. If there is a big enough change within a platform,
 * then we can differentiate USB topologies by varying the HW version field
 * in the Sink and Source Capabilities Extended messages.
 *
 * To reserve a new PID, use go/usb.
 */
#undef CONFIG_USB_PID

/* PPC needs to be informed of CC polarity */
#undef CONFIG_USBC_PPC_POLARITY

/*
 * Disable charging from Default(USB) Rp as a type-c supplier. If your device
 * can detect such a supplier by BC 1.2, define this to get more current
 * from a BC 1.2 supplier.
 */
#undef CONFIG_USBC_DISABLE_CHARGE_FROM_RP_DEF

/* USB Type-C Power Path Controllers (PPC) */
#undef CONFIG_USBC_PPC_AOZ1380
#undef CONFIG_USBC_PPC_NX20P3481
#undef CONFIG_USBC_PPC_NX20P3483
#undef CONFIG_USBC_PPC_SN5S330
#undef CONFIG_USBC_PPC_SYV682X

/*
 * SYV682x PPC high voltage power path current limit.  Default limit is
 * 3.3A.  See the syv682x header file for permissible values.
 */
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_3_30

/* PPC is capable of gating the SBU lines. */
#undef CONFIG_USBC_PPC_SBU

/* PPC is capable of providing VCONN */
#undef CONFIG_USBC_PPC_VCONN

/* PPC has level interrupts and has a dedicated interrupt pin to check */
#undef CONFIG_USBC_PPC_DEDICATED_INT

/* Support for USB type-c superspeed mux */
#undef CONFIG_USBC_SS_MUX

/*
 * Only configure USB type-c superspeed mux when DFP (for chipsets that
 * don't support being a UFP)
 */
#undef CONFIG_USBC_SS_MUX_DFP_ONLY

/* Support v1.1 type-C connection state machine */
#undef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP

/* Support for USB type-c vconn. Not needed for captive cables. */
#undef CONFIG_USBC_VCONN

/* Support VCONN swap */
#undef CONFIG_USBC_VCONN_SWAP

/* USB Binary device Object Store support */
#undef CONFIG_USB_BOS

/* USB Device version of product */
#undef CONFIG_USB_BCD_DEV

/*****************************************************************************/

/* Compile chip support for the USB device controller */
#undef CONFIG_USB

/* Support USB isochronous handler */
#undef CONFIG_USB_ISOCHRONOUS

/* Common USB / BC1.2 charger detection routines */
#undef CONFIG_USB_CHARGER

/*
 * Used for bc1.2 chips that need to be triggered from data role swaps instead
 * of just VBUS changes.
 */
#undef CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER

/*
 * Board only needs one bc12 driver. This includes the case that has multiple
 * chips that use the same driver. Enabled by default.
 *
 * If undefined, board should define a bc12_ports array which associates
 * each port to its bc12 driver.
 */
#define CONFIG_BC12_SINGLE_DRIVER

/* External BC1.2 charger detection devices. */
#undef CONFIG_BC12_DETECT_MAX14637
#undef CONFIG_BC12_DETECT_MT6360
#undef CONFIG_BC12_DETECT_PI3USB9201
#undef CONFIG_BC12_DETECT_PI3USB9281
/* Number of Pericom PI3USB9281 chips present in system */
#undef CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT


/* Enable USB serial console module. */
#undef CONFIG_USB_CONSOLE

/*
 * Enable USB serial console module using usb stream config.
 * NOTE: CONFIG_USB_CONSOLE and CONFIG_USB_CONSOLE_STREAM should be defined
 * exclusively each other.
 */
#undef CONFIG_USB_CONSOLE_STREAM

/* USB serial console transmit buffer size in bytes. */
#define CONFIG_USB_CONSOLE_TX_BUF_SIZE 2048

/*
 * Enable USB serial console crc32 computation.
 * Also makes console output block on overrun.
 */
#undef CONFIG_USB_CONSOLE_CRC

/* Support USB HID interface. */
#undef CONFIG_USB_HID

/* Support USB HID keyboard interface. */
#undef CONFIG_USB_HID_KEYBOARD

/* Support USB HID keyboard backlight. */
#undef CONFIG_USB_HID_KEYBOARD_BACKLIGHT

/* Support USB HID touchpad interface. */
#undef CONFIG_USB_HID_TOUCHPAD

/* HID touchpad logical dimensions */
#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_X
#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_Y
#undef CONFIG_USB_HID_TOUCHPAD_LOGICAL_MAX_PRESSURE
/* HID touchpad physical dimensions (tenth of mm) */
#undef CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_X
#undef CONFIG_USB_HID_TOUCHPAD_PHYSICAL_MAX_Y

/* USB device buffers and descriptors */
#undef CONFIG_USB_RAM_ACCESS_SIZE
#undef CONFIG_USB_RAM_ACCESS_TYPE
#undef CONFIG_USB_RAM_BASE
#undef CONFIG_USB_RAM_SIZE

/* Disable automatic connection of USB peripheral */
#undef CONFIG_USB_INHIBIT_CONNECT

/* Disable automatic initialization of USB peripheral */
#undef CONFIG_USB_INHIBIT_INIT

/* Support simple control of power to the device's USB ports */
#undef CONFIG_USB_PORT_POWER_DUMB

/*
 * Support smart power control to the device's USB ports, using
 * dedicated power control chips.  This potentially enables automatic
 * negotiation of supplying more power to peripherals.
 */
#undef CONFIG_USB_PORT_POWER_SMART

/*
 * GPIOs to enable USB port power have non-const configuration.
 */
#undef CONFIG_USB_PORT_ENABLE_DYNAMIC

/*
 * Support smart power control to the device's USB ports, however only CDP and
 * SDP are supported.  Usually this is the case if all the control lines to the
 * charging port controller are hard-wired.
 */
#undef CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY

/*
 * Override the default charging mode for USB smart power control.
 * Value is selected from usb_charge_mode in include/usb_charge.h
 */
#undef CONFIG_USB_PORT_POWER_SMART_DEFAULT_MODE

/*
 * Smart USB power control can use a full set of control signals to the USB
 * port power chip, or a reduced set.  If this is defined, use the reduced set.
 */
#undef CONFIG_USB_PORT_POWER_SMART_SIMPLE

/*  Number of smart USB power ports. */
#define CONFIG_USB_PORT_POWER_SMART_PORT_COUNT 2

/*
 * Smart USB power control current limit pins may be inverted.  In this case
 * they are active low and the GPIO names will be GPIO_USBn_ILIM_SEL_L.
 */
#undef CONFIG_USB_PORT_POWER_SMART_INVERTED

/*
 * Support waking up host by setting the K-state on the data lines (requires
 * CONFIG_USB_SUSPEND to be set as well).
 */
#undef CONFIG_USB_REMOTE_WAKEUP

/* Support programmable USB device iSerial field. */
#undef CONFIG_USB_SERIALNO

/* Support reporting of configuration bMaxPower in mA */
#define CONFIG_USB_MAXPOWER_MA 500

/* Support reporting as self powered in USB configuration. */
#undef CONFIG_USB_SELF_POWERED

/* Support correct handling of USB suspend (host-initiated). */
#undef CONFIG_USB_SUSPEND

/* Default pull-up value on the USB-C ports when they are used as source. */
#define CONFIG_USB_PD_PULLUP TYPEC_RP_1A5
/*
 * Override the pull-up value when only zero or one port is actively sourcing
 * current and we can advertise more current than what is defined by
 * `CONFIG_USB_PD_PULLUP`.
 * Should be defined with one of the tcpc_rp_value.
 */
#undef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT

/*
 * Total current in mA the board can supply to external devices through
 * USB-C ports
 *
 * When a sink device is plugged or unplugged, source current redistribution
 * occurs. If this macro is defined, redistribution occurs in such a way
 * that there is no current drop (e.g. 3A -> 1.5A) on active source ports.
 */
#undef CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT

/******************************************************************************/
/* stm32f4 dwc usb configs. */

/* Set USB speed to FS rather than HS */
#undef CONFIG_USB_DWC_FS

/******************************************************************************/
/* USB port switch */

/* Allow run-time completion of the usb mux driver structure */
#undef CONFIG_USB_MUX_RUNTIME_CONFIG

/* Support the AMD FP5 USB/DP Mux */
#undef CONFIG_USB_MUX_AMD_FP5

/*
 * Support the Analogix ANX7440 USB Type-C Active mux with
 * Integrated Re-timers for USB3.1/DisplayPort.
 */
#undef CONFIG_USB_MUX_ANX7440

/* Support the ITE IT5205 Type-C USB alternate mode mux. */
#undef CONFIG_USB_MUX_IT5205

/* Support the Pericom PI3USB30532 USB3.0/DP1.2 Matrix Switch */
#undef CONFIG_USB_MUX_PI3USB30532

/* Support the Pericom PI3USB31532 USB3.1/DP1.4 Matrix Switch */
#undef CONFIG_USB_MUX_PI3USB31532

/* Support the Parade PS8740 Type-C Redriving Switch */
#undef CONFIG_USB_MUX_PS8740

/* Support the Parade PS8742 Type-C Redriving Switch */
#undef CONFIG_USB_MUX_PS8742

/* Support the Parade PS8743 Type-C Redriving Switch */
#undef CONFIG_USB_MUX_PS8743

/* 'Virtual' USB mux under host (not EC) control */
#undef CONFIG_USB_MUX_VIRTUAL

/* Enable IT5205H SBU protection switch */
#undef CONFIG_USB_MUX_IT5205H_SBU_OVP

/*****************************************************************************/
/* USB GPIO config */
#undef CONFIG_USB_GPIO

/*****************************************************************************/
/* USB SPI config */
#undef CONFIG_USB_SPI

/*****************************************************************************/
/* USB I2C config */
#undef CONFIG_USB_I2C

/* Allowed read/write count for USB over I2C */
#define CONFIG_USB_I2C_MAX_WRITE_COUNT 60
#define CONFIG_USB_I2C_MAX_READ_COUNT 60

/*****************************************************************************/
/* USB Power monitoring interface config */
#undef CONFIG_USB_POWER

/*****************************************************************************/
/*
 * USB stream signing config. This allows data read over UART or SPI
 * to have a signature generated that can be used to validate the data
 * offline based on H1's registered key. Used by mn50.
 */
#undef CONFIG_STREAM_SIGNATURE


/*****************************************************************************/

/*
 * Support early firmware selection
 *
 * EFS1 is being deprecated. EFS2 is faster, doesn't need two slots, and
 * supports rollback protection.
 *
 * EFS2 runs in the system task (a.k.a. main) and the hook task (for shutdown
 * hook). Their stack sizes must be big enough for sha256.
 */
#undef CONFIG_VBOOT_EFS
#undef CONFIG_VBOOT_EFS2

/* Offset of RW-A image in writable storage when using EFS. */
#undef CONFIG_RW_A_STORAGE_OFF
/* Offset of RW-A signature. */
#undef CONFIG_RW_A_SIGN_STORAGE_OFF
/* Offset of RW-B image in writable storage when using EFS. */
#undef CONFIG_RW_B_STORAGE_OFF
/* Offset of RW-B signature. */
#undef CONFIG_RW_B_SIGN_STORAGE_OFF

/* Support computing hash of code for verified boot */
#undef CONFIG_VBOOT_HASH

/* Support for secure temporary storage for verified boot */
#undef CONFIG_VSTORE

/* Number of supported slots for secure temporary storage */
#undef CONFIG_VSTORE_SLOT_COUNT

/*****************************************************************************/
/* Watchdog config */

/*
 * Compile watchdog timer support.  The watchdog timer will reboot the system
 * if the hook task (which is the lowest-priority task on the system) gets
 * starved for CPU time and isn't able to fire its HOOK_TICK event.
 */
#define CONFIG_WATCHDOG

/*
 * Try to detect a watchdog that is about to fire, and print a trace.  This is
 * required on chips such as STM32 where the watchdog timer simply reboots the
 * system without any early warning.
 */
#undef CONFIG_WATCHDOG_HELP

/*
 * The maximum number of times that the watchdog timer may reset
 * before halting the system (or taking some sort of other
 * chip-dependent corrective action).
 */
#define CONFIG_WATCHDOG_MAX_RETRIES 4

/* Watchdog period in ms; see also AUX_TIMER_PERIOD_MS */
#define CONFIG_WATCHDOG_PERIOD_MS 1600

/*
 * Fire auxiliary timer 500ms before watchdog timer expires. This leaves
 * some time for debug trace to be printed.
 */
#define CONFIG_AUX_TIMER_PERIOD_MS (CONFIG_WATCHDOG_PERIOD_MS - 500)

/*****************************************************************************/
/* WebUSB config */

/*
 * Enable the WebUSB support and define its URL.
 * Export a WebUSB Platform Descriptor in the Binary Object Store descriptor.
 * The WebUSB landing page URL is equal to 'CONFIG_WEBUSB_URL' plus the
 * https:// prefix.
 * This requires CONFIG_USB_BOS.
 */
#undef CONFIG_WEBUSB_URL

/*****************************************************************************/

/*
 * Support controlling power to WiFi, WWAN (3G/LTE), and/or bluetooth modules.
 */
#undef CONFIG_WIRELESS

/*
 * Support for WiFi devices that must remain powered in suspend.  Set to the
 * combination of EC_WIRELESS_SWITCH flags (from ec_commands.h) which should
 * be set in suspend.
 */
#undef CONFIG_WIRELESS_SUSPEND

/* WiFi power control signal is active-low. */
#undef CONFIG_WLAN_POWER_ACTIVE_LOW

/*
 * Write protect signal is active-high.  If this is defined, there must be a
 * GPIO named GPIO_WP; if not defined, there must be a GPIO names GPIO_WP_L.
 */
#undef CONFIG_WP_ACTIVE_HIGH

/*
 * The write protect signal is always asserted,
 * independently of the GPIO existence or current value.
 */
#undef CONFIG_WP_ALWAYS

/*
 * If needed to allocate some free space in the base of the RO or RW section
 * of the image, define these to be equal the required size of the free space.
 */
#define CONFIG_RO_HEAD_ROOM 0
#define CONFIG_RW_HEAD_ROOM 0

/* Firmware upgrade options. */
/* A different config for the same update. TODO(vbendeb): dedupe these */
#undef CONFIG_USB_UPDATE

/* Add support for pairing over the USB update interface. */
#undef CONFIG_USB_PAIRING

/* Add support for reading UART buffer from USB update interface. */
#undef CONFIG_USB_CONSOLE_READ

/* PDU size for fw update over USB (or TPM). */
#define CONFIG_UPDATE_PDU_SIZE 1024

/*
 * If defined, charge_get_state returns a special status if battery is
 * discharging and battery is nearly full.
 */
#undef CONFIG_PWR_STATE_DISCHARGE_FULL

/*
 * Define this if a chip needs to add some information to the common 'version'
 * command output.
 */
#undef CONFIG_EXTENDED_VERSION_INFO

/*
 * Define this to enable Cros Board Info support. I2C_EEPROM_PORT and
 * I2C_EEPROM_ADDR must be defined as well.
 */
#undef CONFIG_CROS_BOARD_INFO

/*****************************************************************************/
/*
 * ISH config defaults
 */
/*
 * This will be automatically defined below if the board supports power
 * modes that will require the AONTASK functionality.
 */
#undef CONFIG_ISH_PM_AONTASK

/*
 * Define the following if the power state support is required.
 */
#undef CONFIG_ISH_PM_D0I1
#undef CONFIG_ISH_PM_D0I2
#undef CONFIG_ISH_PM_D0I3
#undef CONFIG_ISH_PM_D3

/*
 * Define the following if the ip accessible power gating is required.
 */
#undef CONFIG_ISH_IPAPG

/*
 * Define the following to the number of uSeconds of elapsed time that is
 * required to enter D0I2 and D0I3, if they are supported
 */
#undef CONFIG_ISH_D0I2_MIN_USEC
#undef CONFIG_ISH_D0I3_MIN_USEC

/*
 * Define the following if the new specific power management processing
 * after ISH 5.4 is used.
 */
#undef CONFIG_ISH_NEW_PM

/*
 * Define the following in order to perform power management reset
 * prep IRQ setup when entering a new state
 */
#undef CONFIG_ISH_PM_RESET_PREP

/*
 * Define the following if combined ISR is required for ipc communication
 * between host and ISH.
 */
#undef CONFIG_ISH_HOST2ISH_COMBINED_ISR

/*
 * Define the following if there is need to clear ISH fabric error.
 */
#undef CONFIG_ISH_CLEAR_FABRIC_ERRORS

/*
 * Define the following if the version of ISH uses Synopsys Designware uart.
 */
#undef CONFIG_ISH_DW_UART

/*
 * On Intel devices EC's USB-C port numbers may not be physically equal to
 * AP's USB3 & USB2 port number. Because there can be MAX 15 USB2 ports on
 * PCH and MAX 15 USB3 ports on SOC, based on the complexity of the physical
 * layout of the board, USB3 & USB2 port signals of AP are routed to respective
 * USB-C port of EC. Hence, to configure the Intel Virtual MUX, information of
 * USB3 and USB2 port numbers of the respective USB-C port is needed.
 */
#undef CONFIG_INTEL_VIRTUAL_MUX

/*
 * TEST ONLY defines (CONFIG_TEST_*)
 *
 * Used to include files for unit and other builds tests.
 */

 /* Define to enable Policy Engine State Machine. */
#undef CONFIG_TEST_USB_PE_SM

/* Define to enable USB State Machine framework. */
#undef CONFIG_TEST_SM

/*
 * This build is not a complete platform/ec based EC, but instead
 * using the platform/ec zephyr module.
 */
#undef CONFIG_ZEPHYR

/* Define to supprt factory mode */
#undef CONFIG_FACTORY_SUPPORT

/* Define to supprt capsled */
#undef CONFIG_CAPSLED_SUPPORT

/*****************************************************************************/
/*
 * Include board and core configs, since those hold the CONFIG_ constants for a
 * given configuration.  This guarantees they get included everywhere, and
 * fixes a fairly common bug where we gate out code with #ifndef
 * CONFIG_SOMETHING and but forget to include both of these.
 *
 * Board is included after chip, so that chip defaults can be overridden on a
 * per-board basis as needed.
 */
#ifdef __CROS_EC_CONFIG_CHIP_H
#error Include config.h instead of config_chip.h!
#endif
#ifdef __BOARD_H
#error Include config.h instead of board.h!
#endif

#include "config_chip.h"
#include "board.h"

/*
 * Define CONFIG_HOST_ESPI_VW_POWER_SIGNAL if any power signals from the host
 * are configured as virtual wires.
 */
#if defined(CONFIG_HOSTCMD_ESPI_VW_SLP_S3) || \
	defined(CONFIG_HOSTCMD_ESPI_VW_SLP_S4)
#define CONFIG_HOST_ESPI_VW_POWER_SIGNAL
#endif

#if defined(CONFIG_HOST_ESPI_VW_POWER_SIGNAL) && !defined(CONFIG_HOSTCMD_ESPI)
#error Must enable eSPI to enable virtual wires.
#endif

/******************************************************************************/
/*
 * If CONFIG_USB_POWER_DELIVERY is enabled, make sure either
 * CONFIG_USB_PD_TCPMV1 or CONFIG_USB_PD_TCPMV2 is enabled but not both. Also
 * make sure CONFIG_USB_PD_DECODE_SOP is enabled with CONFIG_USB_PD_TCPMV2
 */
#ifdef CONFIG_USB_POWER_DELIVERY
#if defined(CONFIG_USB_PD_TCPMV1) && defined(CONFIG_USB_PD_TCPMV2)
#error Only one version of the USB PD State Machine can be enabled.
#endif
#if !defined(CONFIG_USB_PD_TCPMV1) && !defined(CONFIG_USB_PD_TCPMV2)
#error Please enable CONFIG_USB_PD_TCPMV1 or CONFIG_USB_PD_TCPMV2.
#endif
#if defined(CONFIG_USB_PD_TCPMV2) && !defined(CONFIG_USB_PD_DECODE_SOP)
#error CONFIG_USB_PD_DECODE_SOP must be enabled with the TCPMV2 PD state machine
#endif
#endif

/******************************************************************************/
/*
 * If CONFIG_USB_PD_USB4 is enabled, make sure CONFIG_USBC_SS_MUX and
 * CONFIG_USB_PD_ALT_MODE_DFP is enabled
 */
#ifdef CONFIG_USB_PD_USB4
#if !defined(CONFIG_USBC_SS_MUX)
#error CONFIG_USBC_SS_MUX must be enabled for USB4 mode support
#endif
# if !defined(CONFIG_USB_PD_ALT_MODE_DFP)
#error CONFIG_USB_PD_ALT_MODE_DFP must be enabled for USB4 mode support
#endif
#endif

/******************************************************************************/
/*
 * Automatically define CONFIG_USB_PD_FRS if FRS is enabled in the TCPC or PPC
 */
#if defined(CONFIG_USB_PD_FRS_PPC) || defined(CONFIG_USB_PD_FRS_TCPC)
#define CONFIG_USB_PD_FRS
#endif

/******************************************************************************/
/* Disable extended message support if PD 3.0 support is disabled. */
#ifndef CONFIG_USB_PD_REV30
#undef CONFIG_USB_PD_EXTENDED_MESSAGES
#endif

/******************************************************************************/
/*
 * PD 3.0 only retries in TCPC hardware twice (for a total of 3 attempts), while
 * PD 2.0 retires three times (for a total of 4 attempts).
 *
 * Note must be [0-3] since it must fit within 2 bits.
 */
#ifdef CONFIG_USB_PD_REV30
#define CONFIG_PD_RETRY_COUNT 2
#else
#define CONFIG_PD_RETRY_COUNT 3
#endif

/******************************************************************************/
/*
 * Ensure that CONFIG_USB_PD_TCPMV2 is being used with exactly one device type
 */
#ifdef CONFIG_USB_PD_TCPMV2
#if defined(CONFIG_USB_VPD) + \
	defined(CONFIG_USB_CTVPD) + \
	defined(CONFIG_USB_DRP_ACC_TRYSRC) != 1
#error Must define exactly one CONFIG_USB_ device type.
#endif
#endif

/******************************************************************************/
/*
 * Automatically define CONFIG_HOSTCMD_X86 if either child option is defined.
 * Ensure LPC and eSPI are mutually exclusive
 */
#if defined(CONFIG_HOSTCMD_LPC) || defined(CONFIG_HOSTCMD_ESPI)
#define CONFIG_HOSTCMD_X86
#endif

#if defined(CONFIG_HOSTCMD_LPC) && defined(CONFIG_HOSTCMD_ESPI)
#error Must select only one type of host communication bus.
#endif

#if defined(CONFIG_HOSTCMD_X86) && \
	!defined(CONFIG_HOSTCMD_LPC) && \
	!defined(CONFIG_HOSTCMD_ESPI)
#error Must select one type of host communication bus.
#endif

/******************************************************************************/
/*
 * Set default code ram size unless it's customized by the chip.
 */
#ifndef CONFIG_CODE_RAM_SIZE
#define CONFIG_CODE_RAM_SIZE CONFIG_RO_SIZE
#endif

/******************************************************************************/
/*
 * Set default data ram size unless it's customized by the chip.
 */
#ifndef CONFIG_DATA_RAM_SIZE
#define CONFIG_DATA_RAM_SIZE CONFIG_RAM_SIZE
#endif

/* Automatic configuration of RAM banks **************************************/
/* Assume one RAM bank if not specified, auto-compute number of banks        */
#ifndef CONFIG_RAM_BANK_SIZE
#define CONFIG_RAM_BANK_SIZE	CONFIG_RAM_SIZE
#endif

#ifndef CONFIG_RAM_BANKS
#define CONFIG_RAM_BANKS	(CONFIG_RAM_SIZE / CONFIG_RAM_BANK_SIZE)
#endif

/******************************************************************************/
/*
 * Store panic data at end of memory by default, unless otherwise
 * configured.  This is safe because we don't context switch away from
 * the panic handler before rebooting, and stacks and data start at
 * the beginning of RAM.
 */
#ifndef CONFIG_PANIC_DATA_SIZE
#define CONFIG_PANIC_DATA_SIZE	sizeof(struct panic_data)
#endif

#ifndef CONFIG_PANIC_DATA_BASE
#define CONFIG_PANIC_DATA_BASE (CONFIG_RAM_BASE			\
				+ CONFIG_RAM_SIZE			\
				- CONFIG_PANIC_DATA_SIZE)
#endif

/******************************************************************************/
/*
 * Set minimum shared memory size, unless it is defined in board file.
 */
#ifndef CONFIG_SHAREDMEM_MINIMUM_SIZE
#ifdef CONFIG_COMMON_RUNTIME
/* If RWSIG is used, we may need more space. */
#if defined(CONFIG_RWSIG)
#define CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG (CONFIG_RSA_KEY_SIZE / 8 * 3)
#else
#define CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG 0
#endif

/*
 * We can't use the "MAX" function here, as it is too smart and BUILD_ASSERT
 * calls do not allow it as parameter. BUILD_MAX below works for both compiler
 * and linker.
 */
#define BUILD_MAX(x, y) ((x) > (y) ? (x) : (y))

/* Minimum: 1kb */
#define CONFIG_SHAREDMEM_MINIMUM_SIZE \
	BUILD_MAX(1024, CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG)
#else /* !CONFIG_COMMON_RUNTIME */
/* Without common runtime, we do not have support for shared memory. */
#define CONFIG_SHAREDMEM_MINIMUM_SIZE 0
#endif
#endif /* !CONFIG_SHAREDMEM_MINIMUM_SIZE */


/******************************************************************************/
/*
 * Disable the built-in console history if using the experimental console.
 *
 * The experimental console keeps its own session-persistent history which
 * survives EC reboot.  It also requires CRC8 for command integrity.
 */
#ifdef CONFIG_EXPERIMENTAL_CONSOLE
#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CRC8
#endif /* defined(CONFIG_EXPERIMENTAL_CONSOLE) */


/******************************************************************************/
/*
 * Automatically define common CONFIG_BOARD_VERSION if any specific option is
 * used.
 */

#if defined(CONFIG_BOARD_VERSION_CBI) || \
	defined(CONFIG_BOARD_VERSION_CUSTOM) || \
	defined(CONFIG_BOARD_VERSION_GPIO)
#define CONFIG_BOARD_VERSION
#endif

/******************************************************************************/
/*
 * Thermal throttling AP must have temperature sensor enabled to get
 * the temperature readings.
 */
#if defined(CONFIG_THROTTLE_AP) && !defined(CONFIG_TEMP_SENSOR)
#define CONFIG_TEMP_SENSOR
#endif

/******************************************************************************/
/*
 * DPTF must have temperature sensor enabled to get the readings for
 * generating DPTF thresholds events.
 */
#if defined(CONFIG_DPTF) && !defined(CONFIG_TEMP_SENSOR)
#define CONFIG_TEMP_SENSOR
#endif


/******************************************************************************/
/* The Matrix Keyboard Protocol depends on MKBP events. */
#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_EVENT
#endif

/******************************************************************************/
/* MKBP events delivery methods. */
#ifdef CONFIG_MKBP_EVENT
#if !defined(CONFIG_MKBP_USE_CUSTOM) && \
	!defined(CONFIG_MKBP_USE_HOST_EVENT) && \
	!defined(CONFIG_MKBP_USE_GPIO) && \
	!defined(CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT) && \
	!defined(CONFIG_MKBP_USE_HECI)
#error Please define one of CONFIG_MKBP_USE_* macro.
#endif

#if defined(CONFIG_MKBP_USE_CUSTOM) + \
	defined(CONFIG_MKBP_USE_GPIO) + \
	defined(CONFIG_MKBP_USE_HOST_EVENT) + \
	defined(CONFIG_MKBP_USE_HOST_HECI) > 1
#error Must select only one type of MKBP event delivery method.
#endif
#endif /* CONFIG_MKBP_EVENT */

/******************************************************************************/
/* Set generic orientation config if a specific orientation config is set. */
#if defined(CONFIG_KX022_ORIENTATION_SENSOR) || \
	defined(CONFIG_BMI_ORIENTATION_SENSOR)
#ifndef CONFIG_ACCEL_FIFO
#error CONFIG_ACCEL_FIFO must be defined to use hw orientation sensor support
#endif
#define CONFIG_ORIENTATION_SENSOR
#endif

/*****************************************************************************/
/* Define CONFIG_BATTERY if board has a battery. */
#if defined(CONFIG_BATTERY_BQ20Z453) || \
	defined(CONFIG_BATTERY_BQ27541) || \
	defined(CONFIG_BATTERY_BQ27621) || \
	defined(CONFIG_BATTERY_BQ4050) || \
	defined(CONFIG_BATTERY_MAX17055) || \
	defined(CONFIG_BATTERY_MM8013) || \
	defined(CONFIG_BATTERY_SMART)
#define CONFIG_BATTERY
#endif

/*****************************************************************************/
/* Define CONFIG_USBC_PPC if board has a USB Type-C Power Path Controller. */
#if defined(CONFIG_USBC_PPC_AOZ1380) || \
	defined(CONFIG_USBC_PPC_NX20P3483) || \
	defined(CONFIG_USBC_PPC_SN5S330)
#define CONFIG_USBC_PPC
#endif /* "has a PPC" */

/* The TI SN5S330 supports VCONN and needs to be informed of CC polarity */
#if defined(CONFIG_USBC_PPC_SN5S330)
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_VCONN
#endif

/* The SYV682X supports VCONN and needs to be informed of CC polarity */
#if defined(CONFIG_USBC_PPC_SYV682X)
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_VCONN
#endif

/*****************************************************************************/
/*
 * Define CONFIG_USB_PD_VBUS_MEASURE_CHARGER if the charger on the board
 * supports VBUS measurement.
 */
#if defined(CONFIG_CHARGER_BD9995X) || \
	defined(CONFIG_CHARGER_RT9466) || \
	defined(CONFIG_CHARGER_RT9467) || \
	defined(CONFIG_CHARGER_MT6370) || \
	defined(CONFIG_CHARGER_BQ25710) || \
	defined(CONFIG_CHARGER_ISL9241)
#define CONFIG_USB_PD_VBUS_MEASURE_CHARGER
#endif

/*****************************************************************************/
/*
 * Define CONFIG_USB_PD_VBUS_MEASURE_TCPC if the tcpc on the board supports
 * VBUS measurement.
 */
#if defined(CONFIG_USB_PD_TCPM_FUSB302)
#define CONFIG_USB_PD_VBUS_MEASURE_TCPC
#endif

/*****************************************************************************/
/*
 * Define CONFIG_USB_PD_TCPC_ON_CHIP if we use ITE series TCPM driver
 * on the board.
 */
#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_TCPC_ON_CHIP
#endif

/*****************************************************************************/
/*
 * Define CONFIG_CHARGER_NARROW_VDC for chargers that use a Narrow VDC power
 * architecture.
 */
#if defined(CONFIG_CHARGER_ISL9237) || defined(CONFIG_CHARGER_ISL9238) || \
	defined(CONFIG_CHARGER_ISL9238C) || defined(CONFIG_CHARGER_ISL9241) || \
	defined(CONFIG_CHARGER_RAA489000) || defined(CONFIG_CHARGER_SM5803)
#define CONFIG_CHARGER_NARROW_VDC
#endif

/*****************************************************************************/
/*
 * Define CONFIG_BUTTON_TRIGGERED_RECOVERY if a board has a dedicated recovery
 * button.
 */
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_BUTTON_TRIGGERED_RECOVERY
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */


#if defined(CONFIG_LED_PWM_COUNT)
#define CONFIG_LED_PWM
#endif /* defined(CONFIG_LED_PWM_COUNT) */

#ifdef CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY
#define CONFIG_LED_PWM_CHARGE_STATE_ONLY
#endif

/*****************************************************************************/
/*
 * Define derived configuration options for EC-EC communication
 */
#ifdef CONFIG_EC_EC_COMM_BATTERY
#ifdef CONFIG_EC_EC_COMM_MASTER
#define CONFIG_EC_EC_COMM_BATTERY_MASTER
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 2
#endif

#ifdef CONFIG_EC_EC_COMM_SLAVE
#define CONFIG_EC_EC_COMM_BATTERY_SLAVE
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#endif
#endif /* CONFIG_EC_EC_COMM_BATTERY */

/*****************************************************************************/
/* Define derived USB PD Discharge common path */
#if defined(CONFIG_USB_PD_DISCHARGE_GPIO) || \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC) || \
	defined(CONFIG_USB_PD_DISCHARGE_PPC)
#define CONFIG_USB_PD_DISCHARGE
#endif

/*****************************************************************************/
/* Define derived config options for DP HPD GPIO */
#ifdef CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM
#define CONFIG_USB_PD_DP_HPD_GPIO
#endif

/*****************************************************************************/
/* Define derived thermistor common path */
#ifdef CONFIG_THERMISTOR_NCP15WB
#define CONFIG_THERMISTOR
#endif

/*****************************************************************************/
/* Define derived config options for BC1.2 detection */
#ifdef CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER
#endif

/*****************************************************************************/
/*
 * Define CONFIG_LIBCRYPTOC if a board needs to read secret data from the
 * anti-rollback block.
 */
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
#define CONFIG_LIBCRYPTOC
#endif

/*****************************************************************************/
/* Fingerprint Sensor Configuration */
#if defined(CONFIG_FP_SENSOR_FPC1025) || \
	defined(CONFIG_FP_SENSOR_FPC1035) || \
	defined(CONFIG_FP_SENSOR_FPC1145)
#define CONFIG_FP_SENSOR
#endif

/*****************************************************************************/
/*
 * Handle task-dependent configs.
 *
 * This prevent sub-modules from being compiled when the task and parent module
 * are not present.
 */

#ifndef HAS_TASK_CHIPSET
#undef CONFIG_AP_HANG_DETECT
#undef CONFIG_CHIPSET_APOLLOLAKE
#undef CONFIG_CHIPSET_BRASWELL
#undef CONFIG_CHIPSET_CANNONLAKE
#undef CONFIG_CHIPSET_COMETLAKE
#undef CONFIG_CHIPSET_GEMINILAKE
#undef CONFIG_CHIPSET_ICELAKE
#undef CONFIG_CHIPSET_JASPERLAKE
#undef CONFIG_CHIPSET_MT817X
#undef CONFIG_CHIPSET_MT8183
#undef CONFIG_CHIPSET_MT8192
#undef CONFIG_CHIPSET_RK3399
#undef CONFIG_CHIPSET_RK3288
#undef CONFIG_CHIPSET_SDM845
#undef CONFIG_CHIPSET_SKYLAKE
#undef CONFIG_CHIPSET_STONEY
#undef CONFIG_CHIPSET_TIGERLAKE
#undef CONFIG_POWER_COMMON
#endif

/*
 * If a board has a chipset task, set the minimum charger power required for
 * powering on to 15W.  This is also the highest power discovered over Type-C by
 * analog signaling.  The EC normally does not communicate using USB PD when the
 * system is locked and in RO, so it would not be able to tell if higher power
 * is available.  However, if a 15W charger is discovered, it's likely that the
 * charger does speak USB PD and we would be able to negotiate more power after
 * booting the AP and jumping to EC RW.
 *
 * If a board needs more or less power to power on, they can re-define this
 * value in their board.h file.
 */
#ifdef HAS_TASK_CHIPSET
#ifndef CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15000
#endif /* !defined(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON) */
#endif /* defined(HAS_TASK_CHIPSET) */


#ifdef CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW
# ifndef CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT
#  define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT \
	(CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
# endif
#endif

#ifndef CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON
/*
 * The function of MEASURE_BATTERY_IMBALANCE and these variables is to prevent a
 * battery brownout when the management IC reports a state of charge that is
 * higher than CHARGER_MIN_BAT_PCT_FOR_POWER_ON, but an individual cell is lower
 * than the rest of the pack.  The critical term is MAX_IMBALANCE_MV, which must
 * be small enough to ensure that the system can reliably boot even when the
 * battery total state of charge barely passes the
 * CHARGER_MIN_BAT_PCT_FOR_POWER_ON threshold.
 *
 * Lowering CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON below
 * CHARGER_MIN_BAT_PCT_FOR_POWER_ON disables this check.  Raising it too high
 * may needlessly prevent boot when the lowest cell can still support the
 * system.
 *
 * As this term is lowered and BATTERY_MAX_IMBALANCE_MV is raised, the risk of
 * cell-undervoltage brownout during startup increases.  Raising this term and
 * lowering MAX_IMBALANCE_MV increases the risk of poor UX when the user must
 * wait longer to turn on their device.
 */
#define CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON 5
#endif

#ifndef CONFIG_BATTERY_MAX_IMBALANCE_MV
/*
 * WAG.  Imbalanced battery packs in this situation appear to have balanced
 * charge very quickly after beginning the charging cycle, since dV/dQ rapidly
 * decreases as the cell is charged out of deep discharge.  Increasing the value
 * of CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON will make a system tolerant of
 * larger values of BATTERY_MAX_IMBALANCE_MV.
 */
#define CONFIG_BATTERY_MAX_IMBALANCE_MV 200
#endif

#ifndef HAS_TASK_KEYPROTO
#undef CONFIG_KEYBOARD_PROTOCOL_8042
/*
 * Note that we don't undef CONFIG_KEYBOARD_PROTOCOL_MKBP, because it doesn't
 * have its own task.
 */
#endif

#ifndef HAS_TASK_PDCMD
#undef CONFIG_HOSTCMD_PD
#endif

#if defined(HAS_TASK_PD_INT_C0) || defined(HAS_TASK_PD_INT_C1) || \
	defined(HAS_TASK_PD_INT_C2) || defined(HAS_TASK_PD_INT_C3)
#define CONFIG_HAS_TASK_PD_INT
#endif

#if defined(HAS_TASK_PDCMD) && defined(CONFIG_HAS_TASK_PD_INT)
#error Should not use PDCMD task with PD INT tasks
#endif

/* Certain console cmds are irrelevant without parent modules. */
#ifndef CONFIG_BATTERY
#undef CONFIG_CMD_PWR_AVG
#endif

#ifndef CONFIG_ADC
#undef CONFIG_CMD_ADC
#endif

/*****************************************************************************/
/* Define derived Chipset configs */
#if defined(CONFIG_CHIPSET_APOLLOLAKE) || \
	defined(CONFIG_CHIPSET_GEMINILAKE)
#define CONFIG_CHIPSET_APL_GLK
#endif

#if defined(CONFIG_CHIPSET_JASPERLAKE) || \
	defined(CONFIG_CHIPSET_TIGERLAKE)
#define CONFIG_CHIPSET_ICELAKE
#endif

#if defined(CONFIG_CHIPSET_APL_GLK)
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#endif

#if defined(CONFIG_CHIPSET_APOLLOLAKE) || \
	defined(CONFIG_CHIPSET_BRASWELL) || \
	defined(CONFIG_CHIPSET_CANNONLAKE) || \
	defined(CONFIG_CHIPSET_COMETLAKE) || \
	defined(CONFIG_CHIPSET_COMETLAKE_DISCRETE) || \
	defined(CONFIG_CHIPSET_GEMINILAKE) || \
	defined(CONFIG_CHIPSET_ICELAKE) || \
	defined(CONFIG_CHIPSET_SKYLAKE)
#define CONFIG_POWER_COMMON
#endif

#if defined(CONFIG_CHIPSET_CANNONLAKE) || \
	defined(CONFIG_CHIPSET_ICELAKE) || \
	defined(CONFIG_CHIPSET_SKYLAKE)
#define CONFIG_CHIPSET_X86_RSMRST_DELAY
#endif

#if defined(CONFIG_HOSTCMD_ESPI_VW_SLP_S3) && \
	defined(CONFIG_CHIPSET_SLP_S3_L_OVERRIDE)
#error "Cannot use CONFIG_CHIPSET_SLP_S3_L_OVERRIDE if SLP_S3 is a virtual wire"
#endif

#if defined(CONFIG_POWER_S0IX) && !defined(CONFIG_POWER_TRACK_HOST_SLEEP_STATE)
#error "Must enable CONFIG_POWER_TRACK_HOST_SLEEP_STATE for S0ix"
#endif

#if defined(CONFIG_CHIPSET_SC7180)
#if defined(CONFIG_POWER_SLEEP_FAILURE_DETECTION) && \
	!defined(CONFIG_CHIPSET_RESUME_INIT_HOOK)
#error "Require resume init hook to enable sleep failure detection"
#endif
#if !defined(CONFIG_POWER_SLEEP_FAILURE_DETECTION) && \
	defined(CONFIG_CHIPSET_RESUME_INIT_HOOK)
#error "Don't enable resume init hook unless for sleep failure detection"
#endif
#endif

/*****************************************************************************/

/*
 * Automatically define CONFIG_ACCEL_LIS2D_COMMON if a child option is defined.
 */
#if defined(CONFIG_ACCEL_LIS2DH) || \
	defined(CONFIG_ACCEL_LIS2DE) || \
	defined(CONFIG_ACCEL_LNG2DM)
#define CONFIG_ACCEL_LIS2D_COMMON
#endif

/*
 * Automatically define CONFIG_ACCEL_LIS2DW_COMMON if a child option is defined.
 */
#if defined(CONFIG_ACCEL_LIS2DW12) || \
	defined(CONFIG_ACCEL_LIS2DWL)
#define CONFIG_ACCEL_LIS2DW_COMMON
#endif

/*
 * CONFIG_ACCEL_LIS2DW12 and CONFIG_ACCEL_LIS2DWL can't be defined at the same
 * time.
 */
#if defined(CONFIG_ACCEL_LIS2DW12) && \
	defined(CONFIG_ACCEL_LIS2DWL)
#error "Define only one of CONFIG_ACCEL_LIS2DW12 and CONFIG_ACCEL_LIS2DWL"
#endif

/*****************************************************************************/
/* Define derived seven segment display common path */
#ifdef CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY
#define CONFIG_SEVEN_SEG_DISPLAY
#endif /* CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY */

/*****************************************************************************/
/* Enable PCIE tunneling if the board supports Thunderbolt-Compatible mode */
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
#define CONFIG_USB_PD_PCIE_TUNNELING
#define CONFIG_USB_PD_TBT_GEN3_CAPABLE
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */

/*
 * CONFIG_CHIP_INIT_ROM_REGION requires that the chip has defined a
 * ROM resident region to store the .init_rom section.
 *
 * These sections must also not be zero bytes, which will happen if
 * the program size is the same as the flash size.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION

#ifndef CONFIG_FLASH
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_FLASH
#endif

#ifndef CONFIG_RO_ROM_RESIDENT_SIZE
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_RO_ROM_RESIDENT_SIZE
#endif

#ifndef CONFIG_RW_ROM_RESIDENT_SIZE
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_RW_ROM_RESIDENT_SIZE
#endif


#if (CONFIG_RO_ROM_RESIDENT_SIZE == 0)
#error CONFIG_RO_ROM_RESIDENT_SIZE is 0 with CONFIG_CHIP_INIT_ROM_REGION defined
#endif

#if (CONFIG_RW_ROM_RESIDENT_SIZE == 0)
#error CONFIG_RW_ROM_RESIDENT_SIZE is 0 with CONFIG_CHIP_INIT_ROM_REGION defined
#endif

/*
 * By default, enable storing the .data section on the ROM resident area to
 * save flash space.
 */
#ifdef CONFIG_MAPPED_STORAGE
#define CONFIG_CHIP_DATA_IN_INIT_ROM
#endif
#endif /* CONFIG_CHIP_INIT_ROM_REGION */

/*****************************************************************************/

/*
 * Apply fuzzer and test config overrides last, since fuzzers and tests need to
 * override some of the config flags in non-standard ways to mock only parts of
 * the system.
 */
#include "fuzz_config.h"
#include "test_config.h"

/*
 * Validity checks to make sure some of the configs above make sense.
 */

#if (CONFIG_AUX_TIMER_PERIOD_MS) < ((HOOK_TICK_INTERVAL_MS) * 2)
#error "CONFIG_AUX_TIMER_PERIOD_MS must be at least 2x HOOK_TICK_INTERVAL_MS"
#endif

#ifdef CONFIG_USB_SERIALNO
#define CONFIG_SERIALNO_LEN 28
#endif

#ifdef CONFIG_MAC_ADDR
#define CONFIG_MAC_ADDR_LEN 20
#endif

#ifndef CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ
#define CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ \
	CONFIG_EC_MAX_SENSOR_FREQ_DEFAULT_MILLIHZ
#endif

/* Enable BMI secondary port if needed. */
#if defined(CONFIG_MAG_BMI_BMM150) || \
	defined(CONFIG_MAG_BMI_LIS2MDL)
#define CONFIG_BMI_SEC_I2C
#endif

/* Enable LSM2MDL secondary port if needed. */
#if defined(CONFIG_MAG_LSM6DSM_BMM150) || \
	defined(CONFIG_MAG_LSM6DSM_LIS2MDL)
#define CONFIG_LSM6DSM_SEC_I2C
#endif

/* Load LIS2MDL driver if needed */
#if defined(CONFIG_MAG_BMI_LIS2MDL) || \
	defined(CONFIG_MAG_LSM6DSM_LIS2MDL)
#define CONFIG_MAG_LIS2MDL
#ifndef CONFIG_ACCELGYRO_SEC_ADDR_FLAGS
#error "The i2c address of the magnetometer is not set."
#endif
#endif

/* Load BMM150 driver if needed */
#if defined(CONFIG_MAG_BMI_BMM150) || \
	defined(CONFIG_MAG_LSM6DSM_BMM150)
#define CONFIG_MAG_BMM150
#ifndef CONFIG_ACCELGYRO_SEC_ADDR_FLAGS
#error "The i2c address of the magnetometer is not set."
#endif
#endif

/* Verify sensorhub is enabled */
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
#ifndef CONFIG_SENSORHUB_LSM6DSM
#error "Enable SENSORHUB_LSM6DSM."
#endif
#endif

/* Fill LPC sense data on X86 architecture. */
#ifdef CONFIG_HOSTCMD_X86
#define CONFIG_MOTION_FILL_LPC_SENSE_DATA
#endif

/*
 * TODO(crbug.com/888109): Makes sure RDP as PSTATE is only enabled where it
 * makes sense.
 */
#ifdef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE
#ifdef CONFIG_FLASH_PSTATE
#error "Flash readout protection and PSTATE may not work as intended."
#endif

#if !defined(CHIP_FAMILY_STM32H7) && !defined(CHIP_FAMILY_STM32F4)
#error "Flash readout protection only implemented on STM32H7 and STM32F4."
#endif
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

#if defined(CONFIG_USB_PD_TCPM_ANX3429) || \
	defined(CONFIG_USB_PD_TCPM_ANX740X) || \
	defined(CONFIG_USB_PD_TCPM_ANX7471)
/* Note: ANX7447 is handled by its own driver, not ANX74XX. */
#define CONFIG_USB_PD_TCPM_ANX74XX
#endif

#if defined(CONFIG_DPTF_MULTI_PROFILE) && !defined(CONFIG_DPTF)
#error "CONFIG_DPTF_MULTI_PROFILE can be set only when CONFIG_DPTF is set."
#endif /* CONFIG_DPTF_MULTI_PROFILE && !CONFIG_DPTF */

/*
 * Define the timeout in milliseconds between when the EC receives a suspend
 * command and when the EC times out and asserts wake because the sleep signal
 * SLP_S0 did not assert.
 */
#ifndef CONFIG_SLEEP_TIMEOUT_MS
#define CONFIG_SLEEP_TIMEOUT_MS 10000
#endif

#ifdef CONFIG_PWM_KBLIGHT
#define CONFIG_KEYBOARD_BACKLIGHT
#endif

/*****************************************************************************/
/* ISH power management related definitions */
#if defined(CONFIG_ISH_PM_D0I2) || \
	defined(CONFIG_ISH_PM_D0I3) || \
	defined(CONFIG_ISH_PM_D3) || \
	defined(CONFIG_ISH_PM_RESET_PREP)

#ifndef CONFIG_LOW_POWER_IDLE
#error "Must define CONFIG_LOW_POWER_IDLE if enable ISH low power states"
#endif

#define CONFIG_ISH_PM_AONTASK

#endif

#ifdef CONFIG_ACCEL_FIFO
#if !defined(CONFIG_ACCEL_FIFO_SIZE) || !defined(CONFIG_ACCEL_FIFO_THRES)
#error "Using CONFIG_ACCEL_FIFO, must define _SIZE and _THRES"
#endif

#ifndef CONFIG_TEMP_CACHE_STALE_THRES
#ifdef CONFIG_ONLINE_CALIB
/*
 * Boards may choose to leave this to default and just turn on online
 * calibration, in which case we'll set the threshold to 5 minutes.
 */
#define CONFIG_TEMP_CACHE_STALE_THRES (5 * MINUTE)
#else
/*
 * Boards that use the FIFO and not the online calibration can just leave this
 * at 0.
 */
#define CONFIG_TEMP_CACHE_STALE_THRES 0
#endif /* CONFIG_ONLINE_CALIB */
#endif /* !CONFIG_TEMP_CACHE_STALE_THRES */

#endif /* CONFIG_ACCEL_FIFO */


/*
 * If USB PD Discharge is enabled, verify that CONFIG_USB_PD_DISCHARGE_GPIO
 * and CONFIG_USB_PD_PORT_MAX_COUNT, CONFIG_USB_PD_DISCHARGE_TCPC, or
 * CONFIG_USB_PD_DISCHARGE_PPC is defined.
 */
#ifdef CONFIG_USB_PD_DISCHARGE
#ifdef CONFIG_USB_PD_DISCHARGE_GPIO
#if !defined(CONFIG_USB_PD_PORT_MAX_COUNT)
#error "PD discharge port not defined"
#endif
#else
#if !defined(CONFIG_USB_PD_DISCHARGE_TCPC) && \
	!defined(CONFIG_USB_PD_DISCHARGE_PPC)
#error "PD discharge implementation not defined"
#endif
#endif /* CONFIG_USB_PD_DISCHARGE_GPIO */
#endif /* CONFIG_USB_PD_DISCHARGE */

/* EC Codec Wake-on-Voice related definitions */
#ifdef CONFIG_AUDIO_CODEC_WOV
#define CONFIG_SHA256
#endif

#ifdef CONFIG_SMBUS_PEC
#define CONFIG_CRC8
#endif

#if defined(CONFIG_ONLINE_CALIB) && !defined(CONFIG_FPU)
#error "Online calibration requires CONFIG_FPU"
#endif

/* Set default values for accelerometer calibration if not defined. */
#ifdef CONFIG_ONLINE_CALIB
#ifndef CONFIG_ACCEL_CAL_MIN_TEMP
#define CONFIG_ACCEL_CAL_MIN_TEMP 0.0f
#endif

#ifndef CONFIG_ACCEL_CAL_MAX_TEMP
#define CONFIG_ACCEL_CAL_MAX_TEMP 45.0f
#endif

#ifndef CONFIG_ACCEL_CAL_KASA_RADIUS_THRES
#define CONFIG_ACCEL_CAL_KASA_RADIUS_THRES 0.001f
#endif

#ifndef CONFIG_ACCEL_CAL_NEWTON_RADIUS_THRES
#define CONFIG_ACCEL_CAL_NEWTON_RADIUS_THRES 0.001f
#endif
#endif /* CONFIG_ONLINE_CALIB */

/*
 *  Vivaldi keyboard code to be enabled only if board has selected
 *  CONFIG_KEYBOARD_PROTOCOL_8042 and not disabled CONFIG_KEYBOARD_VIVALDI
 *  explicitly
 */
#ifndef CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_KEYBOARD_VIVALDI
#endif

#if defined(CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
#if defined(CONFIG_USB_PD_TCPM_PS8705) + \
	defined(CONFIG_USB_PD_TCPM_PS8751) + \
	defined(CONFIG_USB_PD_TCPM_PS8755) + \
	defined(CONFIG_USB_PD_TCPM_PS8805) + \
	defined(CONFIG_USB_PD_TCPM_PS8815) < 2
#error "Must select 2 CONFIG_USB_PD_TCPM_PS8* or above if " \
	"CONFIG_USB_PD_TCPM_MULTI_PS8XXX is defined."
#endif
#endif /* CONFIG_USB_PD_TCPM_MULTI_PS8XXX  */

#if defined(CONFIG_USB_PD_TCPM_PS8705) + \
	defined(CONFIG_USB_PD_TCPM_PS8751) + \
	defined(CONFIG_USB_PD_TCPM_PS8755) + \
	defined(CONFIG_USB_PD_TCPM_PS8805) + \
	defined(CONFIG_USB_PD_TCPM_PS8815) > 1
#if !defined(CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
#error "CONFIG_USB_PD_TCPM_MULTI_PS8XXX MUST be defined if more than one " \
	"CONFIG_USB_PD_TCPM_PS8* are intended to support in a board."
#endif
#endif /* defined(CONFIG_USB_PD_TCPM_PS8705) + ... */

/******************************************************************************/
/* Check body detection setup */
#if defined(CONFIG_BODY_DETECTION)
#ifndef CONFIG_BODY_DETECTION_SENSOR
#error CONFIG_BODY_DETECTION_SENSOR must be defined to use body detection
#endif /* ifndef(CONFIG_BODY_DETECTION_SENSOR) */
/* Use default setting if CONFIG_BODY_DETECTION_CUSTOM is not set. */
#ifndef CONFIG_BODY_DETECTION_CUSTOM
#define CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE     250 /* max sensor odr (Hz) */
#define CONFIG_BODY_DETECTION_VAR_THRESHOLD       550 /* (mm/s^2)^2 */
#define CONFIG_BODY_DETECTION_CONFIDENCE_DELTA    525 /* (mm/s^2)^2 */
#define CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR    120 /* % */
#define CONFIG_BODY_DETECTION_ON_BODY_CON         50  /* % */
#define CONFIG_BODY_DETECTION_OFF_BODY_CON        10  /* % */
#define CONFIG_BODY_DETECTION_STATIONARY_DURATION 15  /* second */
#endif /* ifndef(CONFIG_BODY_DETECTION_CUSTOM) */
#endif /* CONFIG_BODY_DETECTION */

#endif  /* __CROS_EC_CONFIG_H */
