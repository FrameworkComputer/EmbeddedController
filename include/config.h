/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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
 * TODO(rspangler): describe all of these.  Also describe the HAS_TASK_* macro
 * and how/when it should be used vs. a config define.
 */

#undef CONFIG_AC_POWER_STATUS
#undef CONFIG_ADC
#undef CONFIG_BACKLIGHT_X86

/*****************************************************************************/
/* Battery config */

/* Compile battery-specific code for these batteries (pick at most one) */
#undef CONFIG_BATTERY_BQ20Z453
#undef CONFIG_BATTERY_FALCO
#undef CONFIG_BATTERY_LINK
#undef CONFIG_BATTERY_PEPPY
#undef CONFIG_BATTERY_SLIPPY
#undef CONFIG_BATTERY_SPRING

/*
 * Battery can check if it's connected.  If defined, charger will check for
 * battery presence before attempting to communicate with it.
 */
#undef CONFIG_BATTERY_CHECK_CONNECTED

/*
 * Compile smart battery support
 *
 * For batteries which support this specification:
 * http://sbs-forum.org/specs/sbdat110.pdf)
 */
#undef CONFIG_BATTERY_SMART

/*****************************************************************************/

#undef CONFIG_BOARD_POST_GPIO_INIT
#undef CONFIG_BOARD_PRE_INIT
#undef CONFIG_BOARD_VERSION

/*****************************************************************************/
/* Charger config */

/* Compile common charge state code */
#undef CONFIG_CHARGER

/* Compile charger-specific code for these chargers (pick at most one) */
#undef CONFIG_CHARGER_BQ24707A
#undef CONFIG_CHARGER_BQ24715
#undef CONFIG_CHARGER_BQ24725
#undef CONFIG_CHARGER_BQ24738
#undef CONFIG_CHARGER_TPS65090

/*
 * Board specific charging current limit, in mA.  If defined, the charge state
 * machine will not allow the battery to request more current than this.
 */
#undef CONFIG_CHARGER_CURRENT_LIMIT

/*
 * Maximum amount of input current the charger can receive, in mA.
 *
 * This value should depend on external power adapter, designed charging
 * voltage, and the maximum power of the running system.
 */
#undef CONFIG_CHARGER_INPUT_CURRENT

/* Value of the charge sense resistor, in mOhms */
#undef CONFIG_CHARGER_SENSE_RESISTOR

/* Value of the input current sense resistor, in mOhms */
#undef CONFIG_CHARGER_SENSE_RESISTOR_AC

/*****************************************************************************/

#undef CONFIG_CHIPSET_GAIA
#undef CONFIG_CHIPSET_HASWELL
#undef CONFIG_CHIPSET_IVYBRIDGE

#undef CONFIG_CMD_COMXTEST
#undef CONFIG_CMD_DISCHARGE_ON_AC /* TODO(rspangler): poorly named */
#undef CONFIG_CMD_ECTEMP
#undef CONFIG_CMD_PLL
#undef CONFIG_CMD_PMU
#undef CONFIG_CMD_POWERLED
#undef CONFIG_CMD_SCRATCHPAD
#undef CONFIG_CMD_SLEEP

/*****************************************************************************/

/*
 * Provide additional help on console commands, such as the supported
 * options/usage.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_CONSOLE_CMDHELP

#undef CONFIG_CONSOLE_RESTRICTED_INPUT
#undef CONFIG_CONSOLE_UART

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
 *	mmfs = 00008200, shcsr = 00000000, hfsr = 40000000, dfsr = 00000008
 *
 * If this is not defined, only a register dump will be printed.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_DEBUG_EXCEPTIONS

/*****************************************************************************/

#undef CONFIG_DMA_HELP
#undef CONFIG_EEPROM
#undef CONFIG_EOPTION

#undef CONFIG_EXTPOWER_FALCO
#undef CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_SNOW
#undef CONFIG_EXTPOWER_USB

#undef CONFIG_FLASH
#undef CONFIG_FLASH_BANK_SIZE
#undef CONFIG_FLASH_BASE
#undef CONFIG_FLASH_ERASED_VALUE32
#undef CONFIG_FLASH_ERASE_SIZE
#undef CONFIG_FLASH_PHYSICAL_SIZE
#undef CONFIG_FLASH_PROTECT_NEXT_BOOT
#undef CONFIG_FLASH_SIZE
#undef CONFIG_FLASH_WRITE_IDEAL_SIZE
#undef CONFIG_FLASH_WRITE_SIZE

#undef CONFIG_FMAP
#undef CONFIG_FORCE_CONSOLE_RESUME
#undef CONFIG_FPU

#undef CONFIG_FW_IMAGE_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_FW_RO_OFF
#undef CONFIG_FW_RO_SIZE
#undef CONFIG_FW_RW_OFF
#undef CONFIG_FW_RW_SIZE
#undef CONFIG_FW_WP_RO_OFF
#undef CONFIG_FW_WP_RO_SIZE

#undef CONFIG_HOSTCMD
#undef CONFIG_HOST_COMMAND_STATUS
#undef CONFIG_HOST_EMU
#undef CONFIG_HOST_UART
#undef CONFIG_HOST_UART1_GPIOS_PB0_1
#undef CONFIG_HOST_UART1_GPIOS_PC4_5
#undef CONFIG_HOST_UART2_GPIOS_PG4_5
#undef CONFIG_HOST_UART_IRQ

#undef CONFIG_I2C
#undef CONFIG_I2C_ARBITRATION
#undef CONFIG_I2C_DEBUG
#undef CONFIG_I2C_DEBUG_PASSTHRU
#undef CONFIG_I2C_HOST_AUTO
#undef CONFIG_I2C_PASSTHROUGH
#undef CONFIG_I2C_PASSTHRU_RESTRICTED

#undef CONFIG_IRQ_COUNT

/*****************************************************************************/
/* Keyboard config */

/* Enable extra debugging output from keyboard modules */
#undef CONFIG_KEYBOARD_DEBUG

/* Compile code for 8042 keyboard protocol */
#undef CONFIG_KEYBOARD_PROTOCOL_8042

/* Compile code for MKBP keyboard protocol */
#undef CONFIG_KEYBOARD_PROTOCOL_MKBP

/*
 * Keyboard config (struct keyboard_scan_config) is in board.c.  If this is
 * not defined, default values from common/keyboard_scan.c will be used.
 */
#undef CONFIG_KEYBOARD_BOARD_CONFIG

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

/*****************************************************************************/

#undef CONFIG_LED_DRIVER_LP5562
#undef CONFIG_LED_FALCO
#undef CONFIG_LED_PEPPY

#undef CONFIG_LID_SWITCH
#undef CONFIG_LOW_POWER_IDLE
#undef CONFIG_LPC
#undef CONFIG_ONEWIRE
#undef CONFIG_ONEWIRE_LED
#undef CONFIG_OVERFLOW_DETECT
#undef CONFIG_PECI

/*****************************************************************************/
/* PMU config */

/*
 * Force switching on and off the FETs on the PMU controlling various power
 * rails during AP startup and shutdown sequences.  This is mainly useful for
 * bringup when we don't have the corresponding sequences in the AP code.
 *
 * Currently supported only on spring platform.
 */
#undef CONFIG_PMU_FORCE_FET

/*
 * Enable hard-resetting the PMU from the EC.  The implementation is rather
 * hacky; it simply shorts out the 3.3V rail to force the PMIC to panic.  We
 * need this unfortunate hack because it's the only way to reset the I2C engine
 * inside the PMU.
 */
#undef CONFIG_PMU_HARD_RESET

/* Compile support for TPS65090 PMU */
#undef CONFIG_PMU_TPS65090

/*****************************************************************************/

#undef CONFIG_POWER_BUTTON
#undef CONFIG_POWER_BUTTON_X86
#undef CONFIG_PSTATE_AT_END
#undef CONFIG_PSTORE
#undef CONFIG_PWM_FAN
#undef CONFIG_PWM_KBLIGHT
#undef CONFIG_RAM_BASE
#undef CONFIG_RAM_SIZE
#undef CONFIG_SAVE_VBOOT_HASH
#undef CONFIG_SPI
#undef CONFIG_STACK_SIZE
#undef CONFIG_SWITCH
#undef CONFIG_SYSTEM_UNLOCKED

/*
 * List of enabled tasks in ascending priority order.  This is normally
 * defined in each board's ec.tasklist file.
 *
 * For each task, use the macro TASK_ALWAYS(n, r, d, s) for base tasks and
 * TASK_NOTEST(n, r, d, s) for tasks that can be excluded in test binaries,
 * where :
 * 'n' is the name of the task
 * 'r' is the main routine of the task
 * 'd' is an opaque parameter passed to the routine at startup
 * 's' is the stack size in bytes; must be a multiple of 8
 */
#undef CONFIG_TASK_LIST

/*
 * Enable task profiling.
 *
 * Boards may #undef this to reduce image size and RAM usage.
 */
#define CONFIG_TASK_PROFILING

#undef CONFIG_TEMP_SENSOR
#undef CONFIG_TEMP_SENSOR_G781
#undef CONFIG_TEMP_SENSOR_TMP006

#undef CONFIG_TEST_TASK_LIST

#undef CONFIG_UART_BAUD_RATE
#undef CONFIG_UART_RX_BUF_SIZE
#undef CONFIG_UART_TX_BUF_SIZE

#undef CONFIG_USB_PORT_POWER_DUMB
#undef CONFIG_USB_PORT_POWER_SMART
#undef CONFIG_USB_SWITCH_TSU6721

#undef CONFIG_USE_CPCIDVI
#undef CONFIG_USE_PLL
#undef CONFIG_WATCHDOG
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_WIRELESS
#undef CONFIG_WP_ACTIVE_HIGH

/*
 * Include board and core configs, since those hold the CONFIG_ constants for a
 * given configuration.  This guarantees they get included everywhere, and
 * fixes a fairly common bug where we gate out code with #ifndef
 * CONFIG_SOMETHING and but forget to include both of these.
 *
 * Board is included after chip, so that chip defaults can be overridden on a
 * per-board basis as needed.
 */
#include "config_chip.h"
#include "board.h"

#endif  /* __CROS_EC_CONFIG_H */
