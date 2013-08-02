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
 * and how/when it should be used vs. a config define.  And BOARD_*, CHIP_*,
 * and CHIP_FAMILY_*.
 */

/* Compile chip support for analog-to-digital convertor */
#undef CONFIG_ADC

/*
 * Compile support for passing backlight-enable signal from x86 chipset through
 * EC.  This allows the EC to gate the backlight-enable signal with the lid
 * switch.
 */
#undef CONFIG_BACKLIGHT_X86

/*****************************************************************************/
/* Battery config */

/* Compile support for the BS20Z453 battery used on some of the ARM laptops */
#undef CONFIG_BATTERY_BQ20Z453

/* Compile mock battery support; used by tests. */
#undef CONFIG_BATTERY_MOCK

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

/* EC has GPIOs attached to board version stuffing resistors */
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
 * Board supports discharge mode.  In this mode, the battery will discharge
 * even if AC is present.  Used for testing.
 */
#undef CONFIG_CHARGER_DISCHARGE_ON_AC

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

/*
 * Maximum time to charge the battery, in hours.
 *
 * If this timeout is reached, the charger will enter force-idle state.
 * If not defined, charger will provide current until the battery asks it to
 * stop.
 */
#undef CONFIG_CHARGER_TIMEOUT_HOURS

/*****************************************************************************/
/* Chipset config */

/* Compile support for the AP chipset; pick at most one */
#undef CONFIG_CHIPSET_GAIA	/* Gaia and Ares (ARM) */
#undef CONFIG_CHIPSET_HASWELL   /* Intel Haswell (x86) */
#undef CONFIG_CHIPSET_IVYBRIDGE /* Intel Ivy Bridge (x86) */

/*
 * Compile common x86 chipset infrastructure.  Required for
 * CONFIG_CHIPSET_HASWELL and CONFIG_CHIPSET_IVYBRIDGE.
 */
#undef CONFIG_CHIPSET_X86

/*****************************************************************************/
/*
 * Optional console commands
 *
 * Defining these options will enable the corresponding command on the EC
 * console.
 */

#undef CONFIG_CMD_COMXTEST
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

/* Max length of a single line of input */
#define CONFIG_CONSOLE_INPUT_LINE_SIZE 80

/*
 * Disable EC console input if the system is locked.  This is needed for
 * security on platforms where the EC console is accessible from outside the
 * case - for example, via a special USB dongle.
 */
#undef CONFIG_CONSOLE_RESTRICTED_INPUT

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

/* Compile extra debugging and tests for the DMA module */
#undef CONFIG_DMA_HELP

/* Compile support for EC chip internal data EEPROM */
#undef CONFIG_EEPROM

/*
 * Compile the eoption module, which provides a higher-level interface to
 * options stored in internal data EEPROM.
 */
#undef CONFIG_EOPTION

/* Compile support for handling turbo-mode chargers */
#undef CONFIG_EXTPOWER_FALCO

/* Compile support for detecting external power presence via a GPIO */
#undef CONFIG_EXTPOWER_GPIO

/*
 * Compile support for providing power to the device via USB.
 *
 * Note that this is NOT the same as providing power FROM the device to USB
 * peripherals such as mice, phones, memory sticks, etc.  That's controlled
 * via the CONFIG_USB_PORT options below.
 */
#undef CONFIG_EXTPOWER_USB

/*****************************************************************************/
/* Flash configuration */

/* Compile support for programming on-chip flash */
#define CONFIG_FLASH

#undef CONFIG_FLASH_BANK_SIZE
#undef CONFIG_FLASH_BASE
#undef CONFIG_FLASH_ERASED_VALUE32
#undef CONFIG_FLASH_ERASE_SIZE
#undef CONFIG_FLASH_PHYSICAL_SIZE
#undef CONFIG_FLASH_PROTECT_NEXT_BOOT
#undef CONFIG_FLASH_SIZE
#undef CONFIG_FLASH_WRITE_IDEAL_SIZE
#undef CONFIG_FLASH_WRITE_SIZE

/*****************************************************************************/

/* Include a flashmap in the compiled firmware image */
#define CONFIG_FMAP

/* Allow EC serial console input to wake up the EC from STOP mode */
#undef CONFIG_FORCE_CONSOLE_RESUME

/* Enable support for floating point unit */
#undef CONFIG_FPU

/*****************************************************************************/
/* Firmware region configuration */

#undef CONFIG_FW_IMAGE_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_FW_RO_OFF
#undef CONFIG_FW_RO_SIZE
#undef CONFIG_FW_RW_OFF
#undef CONFIG_FW_RW_SIZE
#undef CONFIG_FW_WP_RO_OFF
#undef CONFIG_FW_WP_RO_SIZE

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

/*****************************************************************************/
/* I2C configuration */

#undef CONFIG_I2C
#undef CONFIG_I2C_ARBITRATION
#undef CONFIG_I2C_DEBUG
#undef CONFIG_I2C_DEBUG_PASSTHRU
#undef CONFIG_I2C_HOST_AUTO
#undef CONFIG_I2C_PASSTHROUGH
#undef CONFIG_I2C_PASSTHRU_RESTRICTED

/*****************************************************************************/

/* Number of IRQs supported on the EC chip */
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

/* Compile support for LED driver chip(s) */
#undef CONFIG_LED_DRIVER_DS2413  /* Maxim DS2413, on one-wire interface */
#undef CONFIG_LED_DRIVER_LP5562  /* LP5562, on I2C interface */

/*
 * Compile lid switch support.
 *
 * This is enabled by default because all boards other than reference boards
 * are for laptops with lid switchs.  Reference boards #undef it.
 */
#define CONFIG_LID_SWITCH

#undef CONFIG_LOW_POWER_IDLE

/* Compile support for LPC interface */
#undef CONFIG_LPC

/* Compile support for one-wire interface */
#undef CONFIG_ONEWIRE

/* GPIO bank for one-wire interface */
#undef CONFIG_ONEWIRE_BANK

/* Pin mask for one-wire interface */
#undef CONFIG_ONEWIRE_PIN

/* Check for stack overflows on every context switch */
#undef CONFIG_OVERFLOW_DETECT

/* Compile support for PECI interface to x86 processor */
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

/* Compile support for PMU powerinfo host command */
#undef CONFIG_PMU_POWERINFO

/*****************************************************************************/

/* Compile common code to support power button debouncing */
#undef CONFIG_POWER_BUTTON

/* Compile support for sending the power button signal to x86 chipsets */
#undef CONFIG_POWER_BUTTON_X86

/*
 * The EC stores persistent state information for flash write protect in a
 * block of flash.  If this option is defined, the information is in the last
 * bank of flash, instead of the last bank in the nominally read-only section
 * of flash.
 */
#undef CONFIG_PSTATE_AT_END

/*
 * Compile support for using part of the EC's data EEPROM to hold persistent
 * storage for the AP.
 */
#undef CONFIG_PSTORE

/* Compile support for PWM control of cooling fans */
#undef CONFIG_PWM_FAN

/* Compile support for PWM output to keyboard backlight */
#undef CONFIG_PWM_KBLIGHT

/* Base address of RAM for the chip */
#undef CONFIG_RAM_BASE

/* Size of RAM available on the chip, in bytes */
#undef CONFIG_RAM_SIZE

/*
 * If defined, the hash module will save its last computed hash when jumping
 * between EC images.
 */
#undef CONFIG_SAVE_VBOOT_HASH

/* Compile support for SPI interfaces */
#undef CONFIG_SPI

/* Default stack size to use for tasks, in bytes */
#undef CONFIG_STACK_SIZE

/*
 * Compile common code to handle simple switch inputs such as the recovery
 * button input from the servo debug interface.
 */
#undef CONFIG_SWITCH

/*
 * System should remain unlocked even if write protect is enabled.
 *
 * NOTE: This should ONLY be defined during bringup, and should never be
 * defined on a shipping / released platform.
 */
#undef CONFIG_SYSTEM_UNLOCKED

/*****************************************************************************/
/* Task config */

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
 * List of test tasks.  Same format as CONFIG_TASK_LIST, but used to define
 * additional tasks for a unit test.  Normally defined in
 * test/{testname}.tasklist.
 */
#undef CONFIG_TEST_TASK_LIST

/*
 * Enable task profiling.
 *
 * Boards may #undef this to reduce image size and RAM usage.
 */
#define CONFIG_TASK_PROFILING

/*****************************************************************************/
/* Temperature sensor config */

/* Compile common code for temperature sensor support */
#undef CONFIG_TEMP_SENSOR

/* Compile support for particular temperature sensor chips */
#undef CONFIG_TEMP_SENSOR_G781		/* G781 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP006	/* TI TMP006 sensor, on I2C bus */

/*****************************************************************************/
/* UART config */

/* Baud rate for UARTs */
#define CONFIG_UART_BAUD_RATE 115200

/* UART index (number) for EC console */
#undef CONFIG_UART_CONSOLE

/* UART index (number) for host UART, if present */
#undef CONFIG_UART_HOST

/* GPIOs used by host UART.  Choose at most one. */
#undef CONFIG_UART_HOST_GPIOS_PB0_1
#undef CONFIG_UART_HOST_GPIOS_PC4_5
#undef CONFIG_UART_HOST_GPIOS_PG4_5

/*
 * UART transmit buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.
 */
#define CONFIG_UART_TX_BUF_SIZE 512

/*
 * UART receive buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.  Must be larger than
 * CONFIG_CONSOLE_INPUT_LINE_SIZE to copy and paste scripts.
 */
#define CONFIG_UART_RX_BUF_SIZE 128

/*****************************************************************************/

/* Compile support for simple control of power to the device's USB ports */
#undef CONFIG_USB_PORT_POWER_DUMB

/*
 * Compile support for smart power control to the device's USB ports, using
 * dedicated power control chips.  This potentially enables automatic
 * negotiation of supplying more power to peripherals.
 */
#undef CONFIG_USB_PORT_POWER_SMART

/* Compile support for the TSU6721 I2C smart switch */
#undef CONFIG_USB_SWITCH_TSU6721

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

/*****************************************************************************/

/*
 * Compile support for controlling power to WiFi, WWAN (3G/LTE), and/or
 * bluetooth modules.
 */
#undef CONFIG_WIRELESS

/*
 * Write protect signal is active-high.  If this is defined, there must be a
 * GPIO named GPIO_WP; if not defined, there must be a GPIO names GPIO_WP_L.
 */
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
