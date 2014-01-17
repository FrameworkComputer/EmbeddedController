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
 * TODO(crosbug.com/p/23758): Describe all of these.  Also describe the
 * HAS_TASK_* macro and how/when it should be used vs. a config define.  And
 * BOARD_*, CHIP_*, and CHIP_FAMILY_*.
 */

/* Compile chip support for analog-to-digital convertor */
#undef CONFIG_ADC

/*
 * ADC module has certain clock requirement. If this is defined, the ADC module
 * should call clock_enable_module() to configure clock for ADC.
 */
#undef CONFIG_ADC_CLOCK

/*
 * Some ALS modules may be connected to the EC. We need the command, and
 * specific drivers for each module.
 */
#undef CONFIG_ALS
#undef CONFIG_ALS_ISL29035

/* Support AP hang detection host command and state machine */
#undef CONFIG_AP_HANG_DETECT

/*
 * Support controlling the display backlight based on the state of the lid
 * switch.  The EC will disable the backlight when the lid is closed.
 */
#undef CONFIG_BACKLIGHT_LID

/*
 * If defined, EC will enable the backlight signal only if this GPIO is
 * asserted AND the lid is open.  This supports passing the backlight-enable
 * signal from the AP through EC.
 */
#undef CONFIG_BACKLIGHT_REQ_GPIO

/*****************************************************************************/
/* Battery config */

/*
 * Compile battery-specific code.  Choose at most one.
 *
 * Note that some boards have their own unique battery constants / functions.
 * In this case, those are provided in board/(boardname)/battery.c, and none of
 * these are defined.
 */
#undef CONFIG_BATTERY_BQ20Z453	/* BQ20Z453 battery used on some ARM laptops */
#undef CONFIG_BATTERY_BQ27541	/* BQ27541 battery */
#undef CONFIG_BATTERY_LINK	/* Battery used on Link */

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

/*
 * Charger should call battery_vendor_params() to limit/correct the voltage and
 * current requested by the battery pack before acting on the request.
 */
#undef CONFIG_BATTERY_VENDOR_PARAMS

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

/* Permanent LM4 boot configuration */
#undef CONFIG_BOOTCFG_VALUE

/*****************************************************************************/
/* Charger config */

/* Compile common charge state code */
#undef CONFIG_CHARGER

/* Compile charger-specific code for these chargers (pick at most one) */
#undef CONFIG_CHARGER_BQ24707A
#undef CONFIG_CHARGER_BQ24715
#undef CONFIG_CHARGER_BQ24725
#undef CONFIG_CHARGER_BQ24738
#undef CONFIG_CHARGER_TPS65090  /* Note: does not use CONFIG_CHARGER */

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

/*
 * Board has an GPIO pin to enable or disable charging.
 *
 * This GPIO should be named GPIO_CHARGER_EN, if active high. Or
 * GPIO_CHARGER_EN_L if active low.
 */
#undef CONFIG_CHARGER_EN_GPIO

/* Charger enable GPIO is active low */
#undef CONFIG_CHARGER_EN_ACTIVE_LOW

/*****************************************************************************/
/* Chipset config */

/* AP chipset support; pick at most one */
#undef CONFIG_CHIPSET_BAYTRAIL  /* Intel Bay Trail (x86) */
#undef CONFIG_CHIPSET_GAIA	/* Gaia and Ares (ARM) */
#undef CONFIG_CHIPSET_HASWELL   /* Intel Haswell (x86) */
#undef CONFIG_CHIPSET_IVYBRIDGE /* Intel Ivy Bridge (x86) */
#undef CONFIG_CHIPSET_TEGRA	/* Tegra */

/* Compile common x86 chipset infrastructure.  Required for x86 chips. */
#undef CONFIG_CHIPSET_X86

/* Support power rail control */
#define CONFIG_CHIPSET_HAS_PP1350
#define CONFIG_CHIPSET_HAS_PP5000

/* Support chipset throttling */
#undef CONFIG_CHIPSET_CAN_THROTTLE

/*****************************************************************************/
/*
 * Optional console commands
 *
 * Defining these options will enable the corresponding command on the EC
 * console.
 */

#undef CONFIG_CMD_BATDEBUG
#undef CONFIG_CMD_CLOCKGATES
#undef CONFIG_CMD_COMXTEST
#undef CONFIG_CMD_ECTEMP
#undef CONFIG_CMD_GSV
#undef CONFIG_CMD_ILIM
#undef CONFIG_CMD_JUMPTAGS
#undef CONFIG_CMD_PLL
#undef CONFIG_CMD_PMU
#undef CONFIG_CMD_POWERLED
#undef CONFIG_CMD_RTC_ALARM
#undef CONFIG_CMD_SCRATCHPAD
#undef CONFIG_CMD_SLEEP
#undef CONFIG_CMD_STACKOVERFLOW

/*****************************************************************************/

/* Provide common core code to output panic information without interrupts. */
#define CONFIG_COMMON_PANIC_OUTPUT

/* Provide common core code to handle the operating system timers. */
#define CONFIG_COMMON_TIMER

/*****************************************************************************/

/*
 * Provide additional help on console commands, such as the supported
 * options/usage.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_CONSOLE_CMDHELP

/*
 * Number of entries in console history buffer.
 *
 * Boards may #undef this to reduce memory usage.
 */
#define CONFIG_CONSOLE_HISTORY 8

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

/* Check for stack overflows on every context switch */
#define CONFIG_DEBUG_STACK_OVERFLOW

/*****************************************************************************/

/* Support DMA transfers inside the EC */
#undef CONFIG_DMA

/* Compile extra debugging and tests for the DMA module */
#undef CONFIG_DMA_HELP

/* Support EC chip internal data EEPROM */
#undef CONFIG_EEPROM

/*
 * Compile the eoption module, which provides a higher-level interface to
 * options stored in internal data EEPROM.
 */
#undef CONFIG_EOPTION

/* Support turbo-mode chargers */
#undef CONFIG_EXTPOWER_FALCO

/* Support detecting external power presence via a GPIO */
#undef CONFIG_EXTPOWER_GPIO

/*
 * Support detecting external power presence via a pair of GPIOs, as used
 * on Snow.
 */
#undef CONFIG_EXTPOWER_SNOW

/* Support providing power to the device via USB on Spring. */
#undef CONFIG_EXTPOWER_SPRING

/*****************************************************************************/
/* Number of cooling fans. Undef if none. */
#undef CONFIG_FANS

/*
 * Replace the default fan_percent_to_rpm() function with a board-specific
 * implementation in board.c
 */
#undef CONFIG_FAN_RPM_CUSTOM

/*****************************************************************************/
/* Flash configuration */

/* Support programming on-chip flash */
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

/* Enable debugging and profiling statistics for hook functions */
#undef CONFIG_HOOK_DEBUG

/*****************************************************************************/
/* I2C configuration */

#undef CONFIG_I2C
#undef CONFIG_I2C_ARBITRATION
#undef CONFIG_I2C_DEBUG
#undef CONFIG_I2C_DEBUG_PASSTHRU
#undef CONFIG_I2C_PASSTHROUGH
#undef CONFIG_I2C_PASSTHRU_RESTRICTED

/*****************************************************************************/

/* Number of IRQs supported on the EC chip */
#undef CONFIG_IRQ_COUNT

/*****************************************************************************/
/* Keyboard config */

/*
 * The Silego reset chip sits in between the EC and the physical keyboard on
 * column 2.  To save power in low-power modes, some Silego variants require
 * the signal to be inverted so that the open-drain output from the EC isn't
 * costing power due to the pull-up resistor in the Silego.
 */
#undef CONFIG_KEYBOARD_COL2_INVERTED

/* Enable extra debugging output from keyboard modules */
#undef CONFIG_KEYBOARD_DEBUG

/* The board uses a negative edge-triggered GPIO for keyboard interrupts. */
#undef CONFIG_KEYBOARD_IRQ_GPIO

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
 * Minimum CPU clocks between scans.  This ensures that keyboard scanning
 * doesn't starve the other EC tasks of CPU when running at a decreased system
 * clock.
 */
#undef CONFIG_KEYBOARD_POST_SCAN_CLOCKS

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

/* Support common LED interface */
#undef CONFIG_LED_COMMON

/* Support for LED driver chip(s) */
#undef CONFIG_LED_DRIVER_DS2413  /* Maxim DS2413, on one-wire interface */
#undef CONFIG_LED_DRIVER_LP5562  /* LP5562, on I2C interface */

/*
 * Compile lid switch support.
 *
 * This is enabled by default because all boards other than reference boards
 * are for laptops with lid switchs.  Reference boards #undef it.
 */
#define CONFIG_LID_SWITCH

/*
 * Low power idle options. These are disabled by default and all boards that
 * want to use low power idle must define it. When using the LFIOSC, the low
 * frequency clock will be used to conserve even more power when possible.
 */
#undef CONFIG_LOW_POWER_IDLE
#undef CONFIG_LOW_POWER_USE_LFIOSC

/* Support LPC interface */
#undef CONFIG_LPC

/* Support memory protection unit (MPU) */
#undef CONFIG_MPU

/* Support one-wire interface */
#undef CONFIG_ONEWIRE

/* Support PECI interface to x86 processor */
#undef CONFIG_PECI

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

/* Support TPS65090 PMU */
#undef CONFIG_PMU_TPS65090

/*
 * Support PMU powerinfo host and console commands.  Note that the
 * implementation is currently specific to the Pit board, so don't blindly
 * enable this for another board without fixing that first.
 */
#undef CONFIG_PMU_POWERINFO

/*****************************************************************************/

/* Compile common code to support power button debouncing */
#undef CONFIG_POWER_BUTTON

/* Support sending the power button signal to x86 chipsets */
#undef CONFIG_POWER_BUTTON_X86

/*
 * The EC stores persistent state information for flash write protect in a
 * block of flash.  If this option is defined, the information is in the last
 * bank of flash, instead of the last bank in the nominally read-only section
 * of flash.
 */
#undef CONFIG_PSTATE_AT_END

/* Use part of the EC's data EEPROM to hold persistent storage for the AP. */
#undef CONFIG_PSTORE

/*****************************************************************************/
/* Support PWM control */
#undef CONFIG_PWM

/*****************************************************************************/
/* Support PWM output to keyboard backlight */
#undef CONFIG_PWM_KBLIGHT

/* Base address of RAM for the chip */
#undef CONFIG_RAM_BASE

/* Size of RAM available on the chip, in bytes */
#undef CONFIG_RAM_SIZE

/* Support IR357x Link voltage regulator debugging / reprogramming */
#undef CONFIG_REGULATOR_IR357X

/*
 * If defined, the hash module will save its last computed hash when jumping
 * between EC images.
 */
#undef CONFIG_SAVE_VBOOT_HASH

/* Allow the board to use a GPIO for the SCI# signal. */
#undef CONFIG_SCI_GPIO

/* Support SPI interfaces */
#undef CONFIG_SPI

/* Default stack size to use for tasks, in bytes */
#undef CONFIG_STACK_SIZE

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

/* Support particular temperature sensor chips */
#undef CONFIG_TEMP_SENSOR_G781		/* G781 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP006	/* TI TMP006 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP432	/* TI TMP432 sensor, on I2C bus */

/*
 * If defined, active-high GPIO which indicates temperature sensor chips are
 * powered.  If not defined, temperature sensors are assumed to be always
 * powered.
 */
#undef CONFIG_TEMP_SENSOR_POWER_GPIO

/*****************************************************************************/
/* UART config */

/* Baud rate for UARTs */
#define CONFIG_UART_BAUD_RATE 115200

/* UART index (number) for EC console */
#undef CONFIG_UART_CONSOLE

/* UART index (number) for host UART, if present */
#undef CONFIG_UART_HOST

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

/*****************************************************************************/

/* Support simple control of power to the device's USB ports */
#undef CONFIG_USB_PORT_POWER_DUMB

/*
 * Support smart power control to the device's USB ports, using
 * dedicated power control chips.  This potentially enables automatic
 * negotiation of supplying more power to peripherals.
 */
#undef CONFIG_USB_PORT_POWER_SMART

/*
 * Smart USB power control can use a full set of control signals to the USB
 * port power chip, or a reduced set.  If this is defined, use the reduced set.
 */
#undef CONFIG_USB_PORT_POWER_SMART_SIMPLE

/* Support the TSU6721 I2C smart switch */
#undef CONFIG_USB_SWITCH_TSU6721

/*****************************************************************************/

/* Support computing hash of code for verified boot */
#undef CONFIG_VBOOT_HASH

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
 * Support controlling power to WiFi, WWAN (3G/LTE), and/or bluetooth modules.
 */
#undef CONFIG_WIRELESS

/*
 * Support for WiFi devices that must remain powered in suspend.
 */
#undef CONFIG_WIRELESS_SUSPEND_ENABLE_WIFI

/*
 * Write protect signal is active-high.  If this is defined, there must be a
 * GPIO named GPIO_WP; if not defined, there must be a GPIO names GPIO_WP_L.
 */
#undef CONFIG_WP_ACTIVE_HIGH

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
#include "config_chip.h"
#include "board.h"

/*****************************************************************************/
/*
 * Handle task-dependent configs.
 *
 * This prevent sub-modules from being compiled when the task and parent module
 * are not present.
 */

#ifndef HAS_TASK_CHARGER
#undef CONFIG_CHARGER
#undef CONFIG_CHARGER_BQ24192
#undef CONFIG_CHARGER_BQ24707A
#undef CONFIG_CHARGER_BQ24715
#undef CONFIG_CHARGER_BQ24725
#undef CONFIG_CHARGER_BQ24738
#undef CONFIG_CHARGER_TPS65090
#endif

#ifndef HAS_TASK_CHIPSET
#undef CONFIG_CHIPSET_BAYTRAIL
#undef CONFIG_CHIPSET_GAIA
#undef CONFIG_CHIPSET_HASWELL
#undef CONFIG_CHIPSET_IVYBRIDGE
#undef CONFIG_CHIPSET_X86
#undef CONFIG_CHIPSET_TEGRA
#endif

#ifndef HAS_TASK_KEYPROTO
#undef CONFIG_KEYBOARD_PROTOCOL_8042
/*
 * Note that we don't undef CONFIG_KEYBOARD_PROTOCOL_MKBP, because it doesn't
 * have its own task.
 */
#endif

#ifndef HAS_TASK_KEYSCAN
#undef CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_KEYBOARD_PROTOCOL_MKBP
#endif

/*****************************************************************************/
/*
 * Apply test config overrides last, since tests need to override some of the
 * config flags in non-standard ways to mock only parts of the system.
 */
#include "test_config.h"


#endif  /* __CROS_EC_CONFIG_H */
