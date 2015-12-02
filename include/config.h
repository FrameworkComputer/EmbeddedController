/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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

/* Enable accelerometer interrupts. */
#undef CONFIG_ACCEL_INTERRUPTS
/* Add support for sensor FIFO:
 * define the size of the global fifo, must be a power of 2. */
#undef CONFIG_ACCEL_FIFO
/* The amount of free entries that trigger an interrupt to the AP. */
#undef CONFIG_ACCEL_FIFO_THRES

/*
 * Sensors in this mask are in forced mode: they needed to be polled
 * at their data rate frequency.
 */
#undef CONFIG_ACCEL_FORCE_MODE_MASK

/* Specify type of accelerometers attached. */
#undef CONFIG_ACCEL_KXCJ9
#undef CONFIG_ACCEL_KX022
#undef CONFIG_ACCELGYRO_LSM6DS0
#undef CONFIG_ACCELGYRO_BMI160

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

/*
 * Define the event to raise when BMI160 interrupt.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */
#undef CONFIG_ACCELGYRO_BMI160_INT_EVENT

/* Specify type of Gyrometers attached. */
#undef CONFIG_GYRO_L3GD20H

/* Compile chip support for analog-to-digital convertor */
#undef CONFIG_ADC

/*
 * ADC sample time selection. The value is chip-dependent.
 * TODO: Replace this with CONFIG_ADC_PROFILE entries.
 */
#undef CONFIG_ADC_SAMPLE_TIME

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

/*
 * Some ALS modules may be connected to the EC. We need the command, and
 * specific drivers for each module.
 */
#undef CONFIG_ALS
#undef CONFIG_ALS_ISL29035
#undef CONFIG_ALS_OPT3001
/* Define the exact model ID present on the board: SI1141 = 41, SI1142 = 42, */
#undef CONFIG_ALS_SI114X
/* Check if the device revision is supported */
#undef CONFIG_ALS_SI114X_CHECK_REVISION
/*
 * Define the event to raise when BMI160 interrupt.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */
#undef CONFIG_ALS_SI114X_INT_EVENT

/* Define which ALS sensor is used for dimming the lightbar when dark */
#undef CONFIG_ALS_LIGHTBAR_DIMMING

/* Support AP hang detection host command and state machine */
#undef CONFIG_AP_HANG_DETECT

/* Support AP Warm reset Interrupt. */
#undef CONFIG_AP_WARM_RESET_INTERRUPT

/* Allow proprietary communication protocols' extensions. */
#undef CONFIG_EXTENSION_COMMAND
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

/* Support a simple battery. */
#undef CONFIG_BATTERY

/*
 * Compile battery-specific code.
 *
 * Note that some boards have their own unique battery constants / functions.
 * In this case, those are provided in board/(boardname)/battery.c, and none of
 * these are defined.
 */
#undef CONFIG_BATTERY_BQ20Z453
#undef CONFIG_BATTERY_BQ27541
#undef CONFIG_BATTERY_BQ27621
#undef CONFIG_BATTERY_RYU
#undef CONFIG_BATTERY_SAMUS

/* Compile mock battery support; used by tests. */
#undef CONFIG_BATTERY_MOCK

/*
 * Charger should call battery_override_params() to limit/correct the voltage
 * and current requested by the battery pack before acting on the request.
 *
 * This is valid with CONFIG_CHARGER_V1 only.
 */
#undef CONFIG_BATTERY_OVERRIDE_PARAMS

/*
 * If defined, the charger will check for battery presence before attempting
 * to communicate with it. This avoids the 30 second delay when booting
 * without a battery present. Do not use with CONFIG_BATTERY_PRESENT_GPIO.
 *
 * Replace the default battery_is_present() function with a board-specific
 * implementation in board.c
 */
#undef CONFIG_BATTERY_PRESENT_CUSTOM

/*
 * If defined, GPIO which is driven low when battery is present.
 * Charger will check for battery presence before attempting to communicate
 * with it. This avoids the 30 second delay when booting without a battery
 * present. Do not use with CONFIG_BATTERY_PRESENT_CUSTOM.
 */
#undef CONFIG_BATTERY_PRESENT_GPIO

/*
 * Compile smart battery support
 *
 * For batteries which support this specification:
 * http://sbs-forum.org/specs/sbdat110.pdf)
 */
#undef CONFIG_BATTERY_SMART

/*
 * Critical battery shutdown timeout (seconds)
 *
 * If the battery is at extremely low charge (and discharging) or extremely
 * high temperature, the EC will shut itself down. This defines the timeout
 * period in seconds between the critical condition being detected and the
 * EC shutting itself down. Note that if the critical condition is corrected
 * before the timeout expiration, the EC will not shut itself down.
 *
 */
#define CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT 30

/* Perform a battery cut-off when we reach the battery critical level */
#undef CONFIG_BATTERY_CRITICAL_SHUTDOWN_CUT_OFF

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
 * force-applying a charge current.
 */
#undef CONFIG_BATTERY_REVIVE_DISCONNECT

/* Boot header storage offset. */
#undef CONFIG_BOOT_HEADER_STORAGE_OFF

/* Size of boot header in storage. */
#undef CONFIG_BOOT_HEADER_STORAGE_SIZE

/*****************************************************************************/

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

/* EC has GPIOs attached to board version stuffing resistors */
#undef CONFIG_BOARD_VERSION
/* The decoding of the GPIOs defining board version is defined in board code */
#undef CONFIG_BOARD_SPECIFIC_VERSION

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
 * Number of extra buttons not on the keyboard scan matrix. Doesn't include
 * the power button, which has its own handler.
 */
#undef CONFIG_BUTTON_COUNT

/*
 * Enable case close debug (CCD) mode.
 */
#undef CONFIG_CASE_CLOSED_DEBUG

/*
 * Capsense chip has buttons, too.
 */
#undef CONFIG_CAPSENSE

/*****************************************************************************/

/* Compile charge manager */
#undef CONFIG_CHARGE_MANAGER

/* Allow charge manager to default to charging from dual-role partners */
#undef CONFIG_CHARGE_MANAGER_DRP_CHARGING

/* Handle the external power limit host command in charge manager */
#undef CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT

/* Compile input current ramping support */
#undef CONFIG_CHARGE_RAMP

/* The hardware has some input current ramping/back-off mechanism */
#undef CONFIG_CHARGE_RAMP_HW

/*****************************************************************************/
/* Charger config */

/* Compile common charge state code. You must pick an implementation. */
#undef CONFIG_CHARGER
#undef CONFIG_CHARGER_V1
#undef CONFIG_CHARGER_V2

/* Enable charger AMON_BMON ADC current detection */
#undef CONFIG_CHARGER_ADC_AMON_BMON

/* Compile charger-specific code for these chargers (pick at most one) */
#undef CONFIG_CHARGER_BQ24707A
#undef CONFIG_CHARGER_BQ24715
#undef CONFIG_CHARGER_BQ24725
#undef CONFIG_CHARGER_BQ24735
#undef CONFIG_CHARGER_BQ24738
#undef CONFIG_CHARGER_BQ24770
#undef CONFIG_CHARGER_BQ24773
#undef CONFIG_CHARGER_BQ25890
#undef CONFIG_CHARGER_BQ25892
#undef CONFIG_CHARGER_BQ25895
#undef CONFIG_CHARGER_ISL9237
#undef CONFIG_CHARGER_TPS65090  /* Note: does not use CONFIG_CHARGER */

/*
 * BQ2589x IR Compensation settings.
 * Should be the combination of BQ2589X_IR_TREG_xxxC, BQ2589X_IR_VCLAMP_yyyMV
 * and  BQ2589X_IR_BAT_COMP_zzzMOHM.
 */
#undef CONFIG_CHARGER_BQ2589X_IR_COMP
/*
 * BQ2589x 5V boost current limit and voltage.
 * Should be the combination of BQ2589X_BOOSTV_MV(voltage) and
 * BQ2589X_BOOST_LIM_xxxMA.
 */
#undef CONFIG_CHARGER_BQ2589X_BOOST

/*
 * Board specific charging current limit, in mA.  If defined, the charge state
 * machine will not allow the battery to request more current than this.
 */
#undef CONFIG_CHARGER_CURRENT_LIMIT

/* Enable/disable system power monitor PSYS function */
#undef CONFIG_CHARGER_PSYS

/*
 * Board specific charging current termination limit, in mA.  If defined and
 * charger supports setting termination current it should be set during charger
 * init.
 *
 * TODO(tbroch): Only valid for bq2589x currently.  Configure defaults for other
 * charger ICs that support termination currents.
 */
#undef CONFIG_CHARGER_TERM_CURRENT_LIMIT

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
 * Default input current for the board, in mA.
 *
 * This value should depend on external power adapter, designed charging
 * voltage, and the maximum power of the running system. For type-C chargers,
 * this should be set to 512 mA in order to not brown-out low-current USB
 * charge ports.
 */
#undef CONFIG_CHARGER_INPUT_CURRENT

/*
 * Board specific maximum input current limit, in mA.
 */
#undef CONFIG_CHARGER_MAX_INPUT_CURRENT

/* Minimum battery percentage for power on */
#undef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON

/* Narrow VDC power path */
#undef CONFIG_CHARGER_NARROW_VDC

/*
 * Low energy thresholds - when battery level is below BAT_PCT and an external
 * charger provides less than CHG_MW of power, inform the AP of the situation
 * through the LIMIT_POWER host event.
 */
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT
#undef CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW

/*
 * Equivalent of CONFIG_BATTERY_OVERRIDE_PARAMS for use with
 * CONFIG_CHARGER_V2
 */
#undef CONFIG_CHARGER_PROFILE_OVERRIDE

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

/* Enable trickle charging */
#undef CONFIG_TRICKLE_CHARGING

/*****************************************************************************/
/* Chipset config */

/* AP chipset support; pick at most one */
#undef CONFIG_CHIPSET_BAYTRAIL  /* Intel Bay Trail (x86) */
#undef CONFIG_CHIPSET_BRASWELL  /* Intel Braswell (x86) */
#undef CONFIG_CHIPSET_ECDRIVEN  /* Dummy power module */
#undef CONFIG_CHIPSET_GAIA      /* Gaia and Ares (ARM) */
#undef CONFIG_CHIPSET_HASWELL   /* Intel Haswell (x86) */
#undef CONFIG_CHIPSET_MEDIATEK  /* MediaTek MT81xx */
#undef CONFIG_CHIPSET_ROCKCHIP  /* Rockchip rk32xx */
#undef CONFIG_CHIPSET_SKYLAKE   /* Intel Skylake (x86) */
#undef CONFIG_CHIPSET_TEGRA     /* nVidia Tegra 5 */

/* Support chipset throttling */
#undef CONFIG_CHIPSET_CAN_THROTTLE

/* Enable additional chipset debugging */
#undef CONFIG_CHIPSET_DEBUG

/* Enable chipset reset hook, requires a deferrable function */
#undef CONFIG_CHIPSET_RESET_HOOK

/* Support power rail control */
#define CONFIG_CHIPSET_HAS_PP1350
#define CONFIG_CHIPSET_HAS_PP5000

/*****************************************************************************/
/*
 * Chip config for clock circuitry
 *	define = crystal / undef = oscillator
 */
#undef CONFIG_CLOCK_CRYSTAL

/*****************************************************************************/
/* PMIC config */

/* Support firmware long press power-off timer */
#undef CONFIG_PMIC_FW_LONG_PRESS_TIMER

/* Support PMIC power control */
#undef CONFIG_PMIC

/*****************************************************************************/
/*
 * Optional console commands
 *
 * Defining these options will enable the corresponding command on the EC
 * console.
 */

#undef CONFIG_CMD_ACCELS
#undef CONFIG_CMD_ACCEL_INFO
#undef CONFIG_CMD_ALS
#undef CONFIG_CMD_BATDEBUG
#define CONFIG_CMD_CHARGER
#undef CONFIG_CMD_CHGRAMP
#undef CONFIG_CMD_CLOCKGATES
#undef CONFIG_CMD_COMXTEST
#undef CONFIG_CMD_ECTEMP
#define CONFIG_CMD_FASTCHARGE
#undef CONFIG_CMD_FLASH
#undef CONFIG_CMD_FORCETIME
#undef CONFIG_CMD_GSV
#define CONFIG_CMD_HASH
#undef CONFIG_CMD_HOSTCMD
#define CONFIG_CMD_I2C_SCAN
#define CONFIG_CMD_I2C_XFER
#undef CONFIG_CMD_I2CWEDGE
#define CONFIG_CMD_IDLE_STATS
#define CONFIG_CMD_KEYBOARD
#undef CONFIG_CMD_ILIM
#define CONFIG_CMD_INA
#define CONFIG_CMD_REGULATOR
#undef CONFIG_CMD_JUMPTAGS
#undef CONFIG_CMD_LID_ANGLE
#undef CONFIG_CMD_MCDP
#define CONFIG_CMD_PD
#undef CONFIG_CMD_PD_DEV_DUMP_INFO
#undef CONFIG_CMD_PD_FLASH
#undef CONFIG_CMD_PLL
#undef CONFIG_CMD_PMU
#define CONFIG_CMD_POWER_AP
#define CONFIG_CMD_POWERINDEBUG
#undef CONFIG_CMD_POWERLED
#undef CONFIG_CMD_PSYS
#undef CONFIG_CMD_RTC_ALARM
#undef CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_SHMEM
#undef CONFIG_CMD_SLEEP
#undef CONFIG_CMD_SPI_FLASH
#undef CONFIG_CMD_SPI_XFER
#undef CONFIG_CMD_STACKOVERFLOW
#undef CONFIG_CMD_TASKREADY
#define CONFIG_CMD_TEMP_SENSOR
#define CONFIG_CMD_TIMERINFO
#define CONFIG_CMD_TYPEC
#undef CONFIG_CMD_USART_INFO
#undef CONFIG_CMD_USB_PD_PE
#define CONFIG_CMD_USBMUX

/*****************************************************************************/

/* Provide common core code to output panic information without interrupts. */
#define CONFIG_COMMON_PANIC_OUTPUT

/*
 * Store a panic log and halt the system for a software-related reasons, such as
 * stack overflow or assertion failure.
 */
#undef CONFIG_SOFTWARE_PANIC

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
 * Provide common runtime layer code (tasks, hooks ...)
 * You want this unless you are doing a really tiny firmware.
 */
#define CONFIG_COMMON_RUNTIME

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

/*
 * When enabled, do not build RO image from the same set of files as the RW
 * image. Instead define a separate set of object files in the respective
 * build.mk files by adding the objects to the custom-ro_objs-y variable.
 */
#undef CONFIG_CUSTOMIZED_RO

/*
 * When enabled, build in support for hardware crypto; only supported on CR50.
 */
#undef CONFIG_DCRYPTO

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
 *	mmfs = 00008200, shcsr = 00000000, hfsr = 40000000, dfsr = 00000008
 *
 * If this is not defined, only a register dump will be printed.
 *
 * Boards may #undef this to reduce image size.
 */
#define CONFIG_DEBUG_EXCEPTIONS

/* Support Synchronous UART debug printf. */
#undef CONFIG_DEBUG_PRINTF

/* Check for stack overflows on every context switch */
#define CONFIG_DEBUG_STACK_OVERFLOW

/*****************************************************************************/

/* Support DMA transfers inside the EC */
#undef CONFIG_DMA

/* Use the common interrupt handlers for DMA IRQs */
#define CONFIG_DMA_DEFAULT_HANDLERS

/* Compile extra debugging and tests for the DMA module */
#undef CONFIG_DMA_HELP

/* Support EC to Internal bus bridge. */
#undef CONFIG_EC2I

/* Support EC chip internal data EEPROM */
#undef CONFIG_EEPROM

/* Include code for handling external power */
#define CONFIG_EXTPOWER

/* Support detecting external power presence via a GPIO */
#undef CONFIG_EXTPOWER_GPIO

/*****************************************************************************/
/* Number of cooling fans. Undef if none. */
#undef CONFIG_FANS

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

/* Support programming on-chip flash */
#define CONFIG_FLASH

#undef CONFIG_FLASH_BANK_SIZE
#undef CONFIG_FLASH_ERASED_VALUE32
#undef CONFIG_FLASH_ERASE_SIZE

/* Base address of program memory */
#undef CONFIG_PROGRAM_MEMORY_BASE

/*
 * EC code can reside on internal or external storage. Only one of these
 * CONFIGs should be defined.
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
 * Reuse the space that was occupied in RAM by the little firmware (LFW) loader
 * with the section ".bss.slow" instead.
 */
#undef CONFIG_REPLACE_LOADER_WITH_BSS_SLOW

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
 * Board Image ec.bin contains a RO firmware.  If not defined, the image will
 * only contain the RW firmware. The RO firmware comes from another board.
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

/* Which sensor to look for battery tap recognition */
#undef CONFIG_GESTURE_SENSOR_BATTERY_TAP

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

/* Event generated when battery tap is detected */
#undef CONFIG_GESTURE_TAP_EVENT

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
/* Event generated when significant motion is detected. */

#undef CONFIG_GESTURE_SIGMO_EVENT


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

/* Define which index in motion_sensors is in the base. */
#undef CONFIG_SENSOR_BASE

/* Define which index in motion_sensors is in the lid. */
#undef CONFIG_SENSOR_LID

/******************************************************************************/
/* Host to RAM (H2RAM) Memory Mapping */

/* H2RAM Base memory address */
#undef CONFIG_H2RAM_BASE

/* H2RAM Size */
#undef CONFIG_H2RAM_SIZE

/* H2RAM Host LPC I/O base memory address */
#undef CONFIG_H2RAM_HOST_LPC_IO_BASE

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

/* If we have host command task, assume we also are using host events. */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_EVENTS
#else
#undef  CONFIG_HOSTCMD_EVENTS
#endif

/*
 * For ECs where the host command interface is I2C, slave
 * address which the EC will respond to.
 */
#undef CONFIG_HOSTCMD_I2C_SLAVE_ADDR

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

/*
 * Use if PD MCU controls charging (selecting charging port and input
 * current limit).
 */
#undef CONFIG_HOSTCMD_PD_CHG_CTRL

/* Panic when status of PD MCU reflects that it has crashed */
#undef CONFIG_HOSTCMD_PD_PANIC

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

/*
 * Use to define going in to hibernate early if low on battery.
 * CONFIG_HIBERNATE_BATT_PCT specifies the low battery threshold
 * for going into hibernate early, and CONFIG_HIBERNATE_BATT_SEC defines
 * the minimum amount of time to stay in G3 before checking for low
 * battery hibernate.
 */
#undef CONFIG_HIBERNATE_BATT_PCT
#undef CONFIG_HIBERNATE_BATT_SEC

/* For ECs with multiple wakeup pins, define enabled wakeup pins */
#undef CONFIG_HIBERNATE_WAKEUP_PINS

/* Use a hardware specific udelay(). */
#undef CONFIG_HW_SPECIFIC_UDELAY

/*****************************************************************************/
/* I2C configuration */

#undef CONFIG_I2C
#undef CONFIG_I2C_DEBUG
#undef CONFIG_I2C_DEBUG_PASSTHRU
#undef CONFIG_I2C_PASSTHROUGH
#undef CONFIG_I2C_PASSTHRU_RESTRICTED
#undef CONFIG_I2C_VIRTUAL_BATTERY

/* EC uses an I2C master interface */
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
#undef CONFIG_I2C_SCL_GATE_ADDR
#undef CONFIG_I2C_SCL_GATE_GPIO

/*
 * I2C multi-port controller.
 *
 * If CONFIG_I2C_MULTI_PORT_CONTROLLER is defined, a single on-chip I2C
 * controller may have multiple I2C ports attached. Therefore, I2c operations
 * must lock the controller (not just the port) to prevent hardware access
 * conflicts.
 */
#undef CONFIG_I2C_MULTI_PORT_CONTROLLER

/*****************************************************************************/
/* Current/Power monitor */

/*
 * Compile driver for INA219 or INA231. These two flags may not be both
 * defined.
 */
#undef CONFIG_INA219
#undef CONFIG_INA231

/*****************************************************************************/
/* Inductive charging */

/* Enable inductive charging support */
#undef CONFIG_INDUCTIVE_CHARGING

/******************************************************************************/

/* Support NXP PCA9534 I/O expander. */
#undef CONFIG_IO_EXPANDER_PCA9534

/*****************************************************************************/

/* Number of IRQs supported on the EC chip */
#undef CONFIG_IRQ_COUNT

/*
 * This is the block size of the ILM on the it839x chip.
 * The ILM for static code cache, CPU fetch instruction from
 * ILM(ILM -> CPU)instead of flash(flash -> IMMU -> CPU) if enabled.
 */
#undef CONFIG_IT83XX_ILM_BLOCK_SIZE

/* Enable Wake-up control interrupt from KSI */
#undef CONFIG_IT83XX_KEYBOARD_KSI_WUC_INT

/* LPC cycle can wake-up EC from doze / deep doze mode if define it. */
#undef CONFIG_IT83XX_LPC_ACCESS_INT

/* Interrupt for PECI module. (IT839X series and IT838X DX only) */
#undef CONFIG_IT83XX_PECI_WITH_INTERRUPT

/* Allow the host access to the PNPCFG registers */
#undef CONFIG_IT83XX_PNPCFG_HOST_ACCESS

/* To define it, if I2C channel C and PECI used at the same time. */
#undef CONFIG_IT83XX_SMCLK2_ON_GPC7

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
 * Config KSO to start from a different KSO pin. This is to allow some chips
 * to use alternate functions on KSO pins.
 */
#define CONFIG_KEYBOARD_KSO_BASE 0

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

/* Standard LED behavior according to spec given that we have a red-green
 * bicolor led for charging and one power led
 */
#undef CONFIG_LED_POLICY_STD

/*
 * LEDs for LED_POLICY STD may be inverted.  In this case they are active low
 * and the GPIO names will be GPIO_LED..._L.
 */
#undef CONFIG_LED_BAT_ACTIVE_LOW
#undef CONFIG_LED_POWER_ACTIVE_LOW

/* Support for LED driver chip(s) */
#undef CONFIG_LED_DRIVER_DS2413  /* Maxim DS2413, on one-wire interface */
#undef CONFIG_LED_DRIVER_LP5562  /* LP5562, on I2C interface */

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

/*
 * Enable Pseudo G3 (power removed from EC)
 * This requires board specific implementation.
 */
#undef CONFIG_LOW_POWER_PSEUDO_G3

/*
 * Enable deep sleep during S0 (ignores SLEEP_MASK_AP_RUN).
 */
#undef CONFIG_LOW_POWER_S0

/* Support LPC interface */
#undef CONFIG_LPC

/* Base address of low power RAM. */
#undef CONFIG_LPRAM_BASE

/* Size of low power RAM. */
#undef CONFIG_LPRAM_SIZE

/* Use Link-Time Optimizations to try to reduce the firmware code size */
#undef CONFIG_LTO

/* Need for a math library */
#undef CONFIG_MATH_UTIL

/* Include code to do online compass calibration */
#undef CONFIG_MAG_CALIBRATE

/* Presence of a Bosh Sensortec BMM150 magnetometer behind a BMI160. */
#undef CONFIG_MAG_BMI160_BMM150

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

/* Support MKBP event */
#undef CONFIG_MKBP_EVENT

/*
 * With this option, we can define the MKBP wakeup events in this mask (as a
 * white list) in board level, those events allow to interrupt AP during S3.
 */
#undef CONFIG_MKBP_WAKEUP_MASK

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
 * Enable hard-resetting the PMU from the EC.  The implementation is rather
 * hacky; it simply shorts out the 3.3V rail to force the PMIC to panic.  We
 * need this unfortunate hack because it's the only way to reset the I2C engine
 * inside the PMU.
 */
#undef CONFIG_PMU_HARD_RESET

/* Support TPS65090 PMU */
#undef CONFIG_PMU_TPS65090

/* Suport TPS65090 PMU charging LED. */
#undef CONFIG_PMU_TPS65090_CHARGING_LED

/*
 * Support PMU powerinfo host and console commands.  Note that the
 * implementation is currently specific to the Pit board, so don't blindly
 * enable this for another board without fixing that first.
 */
#undef CONFIG_PMU_POWERINFO

/*
 * Enable this config to make console UART self sufficient (no other
 * initialization required before uart_init(), no interrupts, uart_tx_char()
 * does not exit until character finished transmitting).
 *
 * This is useful during early hardware bringup, each platform needs to
 * implement its own code to support this.
 */
#undef CONFIG_POLLING_UART

/* Compile common code to support power button debouncing */
#undef CONFIG_POWER_BUTTON

/* Force the active state of the power button : 0(default if unset) or 1 */
#undef CONFIG_POWER_BUTTON_ACTIVE_STATE

/* Allow the power button to send events while the lid is closed */
#undef CONFIG_POWER_BUTTON_IGNORE_LID

/* Support sending the power button signal to x86 chipsets */
#undef CONFIG_POWER_BUTTON_X86

/* Compile common code for AP power state machine */
#undef CONFIG_POWER_COMMON

/* Disable the power-on transition when the lid is opened */
#undef CONFIG_POWER_IGNORE_LID_OPEN

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
/*****************************************************************************/
/* Support PWM control */
#undef CONFIG_PWM

/* Support PWM control while in low-power idle */
#undef CONFIG_PWM_DSLEEP

/* Define clock input to PWM module. */
#undef CONFIG_PWM_INPUT_LFCLK

/*****************************************************************************/
/* Support PWM output to keyboard backlight */
#undef CONFIG_PWM_KBLIGHT

/* Base address of RAM for the chip */
#undef CONFIG_RAM_BASE

/* Size of RAM available on the chip, in bytes */
#undef CONFIG_RAM_SIZE

/* Support IR357x Link voltage regulator debugging / reprogramming */
#undef CONFIG_REGULATOR_IR357X

/* Support verifying 2048-bit RSA signature */
#undef CONFIG_RSA

/* Define the RSA key size. */
#undef CONFIG_RSA_KEY_SIZE

/*
 * Verify the RW firmware using the RSA signature.
 * (for accessories without software sync)
 */
#undef CONFIG_RWSIG
/*
 * By default the pubkey and sig are put at the end of the first and second
 * half of the total flash, and take up the minimum space possible. You can
 * override those defaults with these.
 */
#undef CONFIG_RO_PUBKEY_ADDR
#undef CONFIG_RO_PUBKEY_SIZE
#undef CONFIG_RW_SIG_ADDR
#undef CONFIG_RW_SIG_SIZE

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

/* Enable smart battery firmware update driver */
#undef CONFIG_SB_FIRMWARE_UPDATE

/* Allow the board to use a GPIO for the SCI# signal. */
#undef CONFIG_SCI_GPIO

/* Support computing SHA-1 hash */
#undef CONFIG_SHA1

/* Support computing of other hash sizes (without the VBOOT code) */
#undef CONFIG_SHA256
#undef CONFIG_SHA384
#undef CONFIG_SHA512

/* Emulate the CLZ (Count Leading Zeros) in software for CPU lacking support */
#undef CONFIG_SOFTWARE_CLZ

/* Support smbus interface */
#undef CONFIG_SMBUS

/* Support SPI interfaces */
#undef CONFIG_SPI

/* Support deprecated SPI protocol version 2. */
#undef CONFIG_SPI_PROTOCOL_V2

/*
 * Support SPI Slave interfaces. The first board supporting this is cr50 and
 * in its parlance SPI_SLAVE is called SPS. This convention might be
 * reconsidered later, and the use of "SPI" in different config options needs
 * to be cleand up. (crbug.com/512613).
 */
#undef CONFIG_SPS

/* Define the SPI port to use to access SPI accelerometer */
#undef CONFIG_SPI_ACCEL_PORT

/* Support SPI flash */
#undef CONFIG_SPI_FLASH

/* Define the SPI port to use to access the flash */
#undef CONFIG_SPI_FLASH_PORT

/* Support W25Q64 SPI flash */
#undef CONFIG_SPI_FLASH_W25Q64

/* Support W25X40 SPI flash */
#undef CONFIG_SPI_FLASH_W25X40

/* Support GD25Q41B SPI flash */
#undef CONFIG_SPI_FLASH_GD25Q41B

/* SPI flash part supports SR2 register */
#undef CONFIG_SPI_FLASH_HAS_SR2

/* SPI master feature */
#undef CONFIG_SPI_MASTER

/* Support SPI masters without GPIO-specified Chip Selects, instead rely on the
 * SPI master port's hardwired CS pin. */
#undef CONFIG_SPI_MASTER_NO_CS_GPIOS

/* Support testing SPI slave controller driver. */
#undef CONFIG_SPS_TEST

/* Default stack size to use for tasks, in bytes */
#undef CONFIG_STACK_SIZE

/* Use 32-bit timer for clock source on stm32. */
#undef CONFIG_STM_HWTIMER32

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
#undef CONFIG_TEMP_SENSOR_BD99992GW	/* BD99992GW PMIC, on I2C bus */
#undef CONFIG_TEMP_SENSOR_EC_ADC        /* Thermistors on EC's own ADC */
#undef CONFIG_TEMP_SENSOR_G781		/* G781 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP006	/* TI TMP006 sensor, on I2C bus */
#undef CONFIG_TEMP_SENSOR_TMP432	/* TI TMP432 sensor, on I2C bus */

#undef CONFIG_THERMISTOR_NCP15WB	/* NCP15WB thermistor */

/*
 * If defined, active-high GPIO which indicates temperature sensor chips are
 * powered.  If not defined, temperature sensors are assumed to be always
 * powered.
 */
#undef CONFIG_TEMP_SENSOR_POWER_GPIO

/*****************************************************************************/
/* TPM-like configuration */

/* Speak the TPM SPI Hardware Protocol on the SPI slave interface */
#undef CONFIG_TPM_SPS

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

/*****************************************************************************/
/* USB PD config */

/* Include all USB Power Delivery modules */
#undef CONFIG_USB_POWER_DELIVERY

/* Support for USB PD alternate mode */
#undef CONFIG_USB_PD_ALT_MODE

/* Support for USB PD alternate mode of Downward Facing Port */
#undef CONFIG_USB_PD_ALT_MODE_DFP

/* Check if max voltage request is allowed before each request */
#undef CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED

/* Default state of PD communication enabled flag */
#define CONFIG_USB_PD_COMM_ENABLED 1

/* Respond to custom vendor-defined messages over PD */
#undef CONFIG_USB_PD_CUSTOM_VDM

/* Default USB data role when a USB PD debug accessory is seen */
#define CONFIG_USB_PD_DEBUG_DR PD_ROLE_DFP

/* Define if this board can act as a dual-role PD port (source and sink) */
#undef CONFIG_USB_PD_DUAL_ROLE

/* Dynamic USB PD source capability */
#undef CONFIG_USB_PD_DYNAMIC_SRC_CAP

/* Support USB PD flash. */
#undef CONFIG_USB_PD_FLASH

/* Check whether PD is the sole power source before flash erase operation */
#undef CONFIG_USB_PD_FLASH_ERASE_CHECK

/* Major and Minor ChromeOS specific PD device Hardware IDs. */
#undef CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR
#undef CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR

/* HW & SW version for alternate mode discover identity response (4bits each) */
#undef CONFIG_USB_PD_IDENTITY_HW_VERS
#undef CONFIG_USB_PD_IDENTITY_SW_VERS

/* USB PD MCU slave address for host commands */
#define CONFIG_USB_PD_I2C_SLAVE_ADDR 0x3c

/* Define if using internal comparator for PD receive */
#undef CONFIG_USB_PD_INTERNAL_COMP

/* Record main PD events in a circular buffer */
#undef CONFIG_USB_PD_LOGGING

/* The size in bytes of the FIFO used for PD events logging */
#undef CONFIG_USB_PD_LOG_SIZE

/* Save power by waking up on VBUS rather than polling CC */
#define CONFIG_USB_PD_LOW_POWER

/* Allow chip to go into low power idle even when a PD device is attached */
#undef CONFIG_USB_PD_LOW_POWER_IDLE_WHEN_CONNECTED

/* Define if USB-PD device has no way of detecting USB VBUS */
#undef CONFIG_USB_PD_NO_VBUS_DETECT

/* Number of USB PD ports */
#undef CONFIG_USB_PD_PORT_COUNT

/* Simple DFP, such as power adapter, will not send discovery VDM on connect */
#undef CONFIG_USB_PD_SIMPLE_DFP

/* Use comparator module for PD RX interrupt */
#define CONFIG_USB_PD_RX_COMP_IRQ

/* Use TCPC module (type-C port controller) */
#undef CONFIG_USB_PD_TCPC

/*
 * Choose one of the following TCPMs (type-C port manager) to manage TCPC. The
 * TCPM stub is used to make direct function calls to TCPC when TCPC is on
 * the same MCU. The TCPCI TCPM uses the standard TCPCI i2c interface to TCPC.
 */
#undef CONFIG_USB_PD_TCPM_STUB
#undef CONFIG_USB_PD_TCPM_TCPCI
#undef CONFIG_USB_PD_TCPM_FUSB302

/*
 * Use this option if the TCPC port controller is on a seperate chip from
 * the TCPM layer and if VUBS detect GPIO is not available on the TCPM
 * mcu.
 */
#undef CONFIG_USB_PD_TCPM_VBUS

/* Define the type-c port controller I2C base address. */
#define CONFIG_TCPC_I2C_BASE_ADDR 0x9c

/* Use this option to enable Try.SRC mode for Dual Role devices */
#undef CONFIG_USB_PD_TRY_SRC

/* Set the default minimum battery percentage for Try.Src to be enabled */
#define CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC 1

/* Alternative configuration keeping only the TX part of PHY */
#undef CONFIG_USB_PD_TX_PHY_ONLY

/* Use DAC as reference for comparator at 850mV. */
#undef CONFIG_PD_USE_DAC_AS_REF

/* USB Product ID. */
#undef CONFIG_USB_PID

/* Support for USB type-c superspeed mux */
#undef CONFIG_USBC_SS_MUX

/*
 * Only configure USB type-c superspeed mux when DFP (for chipsets that
 * don't support being a UFP)
 */
#undef CONFIG_USBC_SS_MUX_DFP_ONLY

/* Sniffer header version
 * Version 1: [timestamp:2B, sequence number:2B]
 * Version 2: [timestamp:2B, sequence number:2B,
 *             Vbus value: 2B, vbus timestamp offset: 2B]
 */
#undef CONFIG_USBC_SNIFFER_HEADER_V1
#undef CONFIG_USBC_SNIFFER_HEADER_V2

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

/* Support USB blob handler. */
#undef CONFIG_USB_BLOB

/* Common USB / BC1.2 charger task */
#undef CONFIG_USB_CHARGER

/* Enable USB serial console module. */
#undef CONFIG_USB_CONSOLE

/* Support USB HID interface. */
#undef CONFIG_USB_HID

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
 * Support supplying USB power in S3, if the host leaves the port enabled when
 * entering S3.
 */
#undef CONFIG_USB_PORT_POWER_IN_S3

/*
 * Support smart power control to the device's USB ports, using
 * dedicated power control chips.  This potentially enables automatic
 * negotiation of supplying more power to peripherals.
 */
#undef CONFIG_USB_PORT_POWER_SMART

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

/*
 * Smart USB power control current limit pins may be inverted.  In this case
 * they are active low and the GPIO names will be GPIO_USBn_ILIM_SEL_L.
 */
#undef CONFIG_USB_PORT_POWER_SMART_INVERTED

/******************************************************************************/
/* USB port switch */

/* 8-bit USB type-C switch I2C addresses */
#undef CONFIG_USB_SWITCH_I2C_ADDRS

/* Support the TSU6721 I2C smart switch */
#undef CONFIG_USB_SWITCH_TSU6721

/* Support the Pericom PI3USB9281 I2C USB switch */
#undef CONFIG_USB_SWITCH_PI3USB9281

/* Number of Pericom PI3USB9281 chips present in system */
#undef CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT

/* Support the Pericom PI3USB30532 USB3.0/DP1.2 Matrix Switch */
#undef CONFIG_USB_MUX_PI3USB30532

/* Support the Parade PS8740 Type-C Redriving Switch */
#undef CONFIG_USB_MUX_PS8740

/*****************************************************************************/
/* USB GPIO config */
#undef CONFIG_USB_GPIO

/*****************************************************************************/
/* USB SPI config */
#undef CONFIG_USB_SPI

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

/* Watchdog period in ms; see also AUX_TIMER_PERIOD_MS */
#define CONFIG_WATCHDOG_PERIOD_MS 1600

/*
 * Fire auxiliary timer 500ms before watchdog timer expires. This leaves
 * some time for debug trace to be printed.
 */
#define CONFIG_AUX_TIMER_PERIOD_MS (CONFIG_WATCHDOG_PERIOD_MS - 500)

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

/*
 * Write protect signal is active-high.  If this is defined, there must be a
 * GPIO named GPIO_WP; if not defined, there must be a GPIO names GPIO_WP_L.
 */
#undef CONFIG_WP_ACTIVE_HIGH

/*
 * The write protect signal is always asserted,
 * independantly of the GPIO existence or current value.
 */
#undef CONFIG_WP_ALWAYS

/*
 * If needed to allocate some free space in the base of the RO or RW section
 * of the image, define these to be equal the required size of the free space.
 */
#undef CONFIG_RO_HEAD_ROOM
#undef CONFIG_RW_HEAD_ROOM

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

/*****************************************************************************/
/*
 * Handle task-dependent configs.
 *
 * This prevent sub-modules from being compiled when the task and parent module
 * are not present.
 */

#ifndef HAS_TASK_CHIPSET
#undef CONFIG_CHIPSET_BAYTRAIL
#undef CONFIG_CHIPSET_BRASWELL
#undef CONFIG_CHIPSET_GAIA
#undef CONFIG_CHIPSET_HASWELL
#undef CONFIG_CHIPSET_MEDIATEK
#undef CONFIG_CHIPSET_ROCKCHIP
#undef CONFIG_CHIPSET_SKYLAKE
#undef CONFIG_CHIPSET_TEGRA
#undef CONFIG_POWER_COMMON
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

#ifndef HAS_TASK_PDCMD
#undef CONFIG_HOSTCMD_PD
#endif

/*****************************************************************************/
/*
 * Apply test config overrides last, since tests need to override some of the
 * config flags in non-standard ways to mock only parts of the system.
 */
#include "test_config.h"

/*
 * Sanity checks to make sure some of the configs above make sense.
 */

#if (CONFIG_AUX_TIMER_PERIOD_MS) < ((HOOK_TICK_INTERVAL_MS) * 2)
#error "CONFIG_AUX_TIMER_PERIOD_MS must be at least 2x HOOK_TICK_INTERVAL_MS"
#endif

#endif  /* __CROS_EC_CONFIG_H */
