# Reducing the EC image size

The EC ToT codebase continues grows as new features are added and for bug fixes.
This puts pressure on older boards that have limited flash space remaining. This
document provides some tips for reducing the EC image size.

[TOC]

## Checking the EC image footprint

The EC codebase supports two build types:

1.  `cros-ec` builds are the legacy EC images built using Make (e.g. `make
    BOARD=volteer`)
1.  `zephyr-ec` builds are the EC images built using the Zephyr RTOS kernel
    using zmake/Cmake (e.g. `zmake build herobrine`)

### Checking a single cros-ec build

Building a single cros-ec board using `make BOARD=<board> -j` reports the the
number of bytes free in flash and RAM for both the RO and RW images. An example
from building the juniper board is shown below.

```
$ make BOARD=juniper -j
    ...
  *** 668 bytes in flash and 10308 bytes in RAM still available on juniper RO ****
  *** 3224 bytes in flash and 7460 bytes in RAM still available on juniper RW ****
```

### Checking all cros-ec builds

Running `make buildall -j` shows a summary of the three boards with the smallest
RO flash footprint, FW flash footprint, and RW RAM footprint.

```
$ make buildall -j
    ...
buildall completed successfully!
Smallest free spaces in RO flash (bytes):
servo_v4  :    104
scarlet   :    108
mushu     :    160
Smallest free spaces in RW flash (bytes):
mushu     :     96
bobba     :    232
trondo    :    376
Tightest boards' RW RAM images, bytes free:
whiskers  :    244
minimuffin:    284
zinger    :    284
```

### Comparing cros-ec image sizes

The cros-ec makefile provides two make targets for helping track the impact of
code changes.

`make savesizes` saves the EC footprint information for all boards, providing
the baseline for comparison. `make newsizes` compares the sizes of the current
build against the EC footprint information saved by most recent invocation of
`make savesizes`.

General workflow:

1.  Checkout branch you need to compare against. For example `repo start
    check-ec-size -r cros/main` or `repo start check-ec-size -r <hash>`.
1.  Run `make buildall -j`.
1.  Run `make savesizes`.
1.  Apply your code change (e.g. change the local branch, cherry-pick your
    changes, or directly edit source files).
1.  Run `make buildall -j` again.
1.  Run `make newsizes` to generate report of size changes.

Example report from `make newsizes` shown below:

```
$ make newsizes
build/burnet/RO/space_free_flash grew by 576 bytes: (488 to 1064)
build/burnet/RW/space_free_flash grew by 552 bytes: (1324 to 1876)
build/cerise/RO/space_free_flash grew by 512 bytes: (276 to 788)
build/cerise/RW/space_free_flash grew by 548 bytes: (7076 to 7624)
    ...
```

### Checking a single zephyr-ec build

By default, `zmake` will display the flash and SRAM usage of the board.

```
$ zmake build herobrine
    ...
Building herobrine:ro: /usr/bin/ninja -C /mnt/host/source/src/platform/ec/build/zephyr/herobrine/build-ro
-- git describe warned: warning: tag 'upstream/v2.7.99' is externally known as 'v2.7.99'
-- Zephyr version: 3.0.99 (/mnt/host/source/src/third_party/zephyr/main), build: v2.7.99-5035-ga17c05c7228e
Memory region         Used Size  Region Size  %age Used
           FLASH:      219920 B       256 KB     83.89%
            SRAM:       49688 B        64 KB     75.82%
        IDT_LIST:          0 GB         2 KB      0.00%
Building herobrine:rw: /usr/bin/ninja -C /mnt/host/source/src/platform/ec/build/zephyr/herobrine/build-rw
-- git describe warned: warning: tag 'upstream/v2.7.99' is externally known as 'v2.7.99'
-- Zephyr version: 3.0.99 (/mnt/host/source/src/third_party/zephyr/main), build: v2.7.99-5035-ga17c05c7228e
Memory region         Used Size  Region Size  %age Used
           FLASH:      219920 B       256 KB     83.89%
            SRAM:       49688 B        64 KB     75.82%
        IDT_LIST:          0 GB         2 KB      0.00%
```

Note, that the flash region size listed above represents the total flash
available on the EC. The actual available region size is only half the reported
value in order to store two images (RO+RW).

#### Other Zephyr utilities

The Cmake system utilized by Zephyr provides two build targets `rom_report` and
`ram_report` which generate a list of all the compiled objects in tabular form.
This can be useful for identifying particular modules that contribute to the
image size.

The `rom_report` and `ram_report` targets are currently only supported when
working outside the chroot. Follow the [instructions][1] for building zephyr-ec
images outside chroot before running the commands below.

```
# Configure the Herobrine zephyr project, storing the build files in /tmp/zephyr-herobrine
$ zmake configure -B /tmp/zephyr-herobrine herobrine

# Build the RO image
$ ninja -C /tmp/zephyr-herobrine/herobrine/build-ro

# Generate the ROM report, report sent to stdout
$ ninja -C /tmp/zephyr-herobrine/herobrine/build-ro rom_report
```

Please refer to the [Zephyr Optimization Tools][3] documentation for details on
the `rom_report` and `ram_report` targets.

## Checking a board's config

If you want to see what configs are enabled for a given board, use the
`print-configs` `Makefile` target:

```shell
$ make BOARD=<BOARD> print-configs
```

You can also open the `./build/<BOARD>/.config` file that is generated after
building the board with `make BOARD=<BOARD>`.

## Disable console commands

The lowest hanging fruit for reducing the EC image size is by disabling console
commands that provide debug information only and don't impact the user or the
automated testing. Any console command that is not used by the FAFT tests and
suites is safe to disable in the EC images.

For cros-ec builds, add `#undef CONFIG_CMD_<name>` to the board.h or baseboard.h
file to disable the console command.

For zephyr-ec builds, add `CONFIG_PLATFORM_EC_CONSOLE_CMD_<name>=n` to the board
prj.conf file to disable the console command.

*   TODO: Create new CONFIG/Kconfig option that disables all console commands
    not required by FAFT.

<!-- mdformat off(Don't format table) -->

| Used by FAFT | config.h option | Console commands | Notes |
|:---|:---|:---|:---|
|  | CONFIG_CMD_ACCELS | `accelrange`<br>`accelres`<br>`accelrate`<br>`accelread`<br>`accelinit`<br>`accelinfo` | |
|  | CONFIG_CMD_ACCELSPOOF | `accelspoof` | |
| | CONFIG_CMD_ACCEL_FIFO | `fiforead` | |
| | CONFIG_CMD_ACCEL_INFO | `accelinfo` | |
| | CONFIG_CMD_ADC | `adc` | Note firmware.ECADC uses the `temps` command. |
| | CONFIG_CMD_ALS | `als` | |
| | CONFIG_CMD_APTHROTTLE | `apthrottle` | |
| | CONFIG_CMD_AP_RESET_LOG |??? | |
| | CONFIG_CMD_BATDEBUG | `fgunseal`<br>`fgseal`<br>`fginit`<br>`fgprobe`<br>`fgrd`<br>`fgcmd`<br>`fcmdrd` | |
| | CONFIG_CMD_BATTFAKE | `battfake` | |
| | CONFIG_CMD_BATT_MFG_ACCESS | `battmfgacc` | |
| | CONFIG_CMD_CBI | `cbi` | firmwareECCbiEeprom uses `ectool` on AP to test CBI |
| x | CONFIG_CMD_CHARGEN | `chargen` | Used by firmware_Cr50CCDUartStress, included in faft_ccd, faft_cr50_prepvt, and faft_cr50_pvt suites |
| | CONFIG_CMD_CHARGER | `bd9995x`<br>`sy21612` | |
| | CONFIG_CMD_CHARGER_ADC_AMON_BMON | `amonbmon` | |
| | CONFIG_CMD_CHARGER_DUMP | `charger_dump` | |
| | CONFIG_CMD_CHARGER_PROFILE_OVERRIDE | `fastcharge` | |
| | CONFIG_CMD_CHARGER_PROFILE_OVERRIDE_TEST | `fastchgtest` | |
| | CONFIG_CMD_CHARGE_SUPPLIER_INFO | `chgsup` | |
| | CONFIG_CMD_CHGRAMP | `chgramp` | |
| | CONFIG_CMD_CLOCKGATES | `clockgates` | |
| | CONFIG_CMD_COMXTEST | `comxtest` | |
| x | CONFIG_CMD_CRASH | `crash` | Used by TAST `crash.ECCrash`, FAFT `firmware_ECSharedMem` |
| | CONFIG_CMD_DEVICE_EVENT | `deviceevent` | |
| | CONFIG_CMD_DLOG | `dlog` | |
| | CONFIG_CMD_ECTEMP | `ectemp` | |
| | CONFIG_CMD_FASTCHARGE | `fastcharge` | Obsolete? use CONFIG_CMD_CHARGER_PROFILE_OVERRIDE? |
| | CONFIG_CMD_FLASH | `flasherase`<br>`flashwrite`<br>`flashread` | |
| x | CONFIG_CMD_FLASHINFO | `flashinfo` | Used by TAST `firmware.ECSize` |
| | CONFIG_CMD_FLASH_TRISTATE | `fpcapture`<br>`flash_tristate` | |
| | CONFIG_CMD_FLASH_WP | `flashwp` | |
| | CONFIG_CMD_FORCETIME | `forcetime` | |
| | CONFIG_CMD_FPSENSOR_DEBUG | `fpcapture`<br>`fpenroll`<br>`fpmatch`<br>`fpclear`<br>`fpmaintenance` | |
| | CONFIG_CMD_GETTIME | `gettime` | Used by Cr50 tests, not by FAFT EC |
| | CONFIG_CMD_GL3590 | `gl3590` | |
| | CONFIG_CMD_GPIO_EXTENDED | Adds options to `gpioget` and `gpioset`. | Should be renamed to CONFIG_GPOI_EXTENDED |
| | CONFIG_CMD_GPIO_POWER_DOWN | Not a valid config. | Should be CONFIG_GPIO_POWER_DOWN |
| | CONFIG_CMD_GT7288 | `gt7288_desc`<br>`gt7288_repdesc`<br>`gt7288_ver`<br>`gt7288_report` | |
| | CONFIG_CMD_HASH | `hash` | tast.firmware.SoftwareSync uses `ectool echash` |
| x | CONFIG_CMD_HCDEBUG | `hcdebug` | firmware_ECBootTime.py |
| x | CONFIG_CMD_HOSTCMD | `hostcmd` | |
| | CONFIG_CMD_I2CWEDGE | `i2cwedge`<br>`i2cunwedge` | |
| | CONFIG_CMD_I2C_PROTECT | `i2cprotect` | |
| | CONFIG_CMD_I2C_SCAN | `i2cscan` | |
| | CONFIG_CMD_I2C_STRESS_TEST | `i2ctest` | |
| | CONFIG_CMD_I2C_STRESS_TEST_ACCEL | Not a console command | |
| | CONFIG_CMD_I2C_STRESS_TEST_ALS | Not a console command | |
| | CONFIG_CMD_I2C_STRESS_TEST_BATTERY | Not a console command | |
| | CONFIG_CMD_I2C_STRESS_TEST_CHARGER | Not a console command | |
| | CONFIG_CMD_I2C_STRESS_TEST_TCPC | `Not a console command | |
| | CONFIG_CMD_I2C_XFER | `i2cxfer` | firmware_ECCbiEeprom uses `ectool i2cxfer` which is not guarded by CONFIG_CMD_I2C_XFER |
| | CONFIG_CMD_I2C_XFER_RAW |  | Adds options to `i2cxfer` |
| | CONFIG_CMD_IDLE_STATS | `idlestats` | |
| | CONFIG_CMD_INA | `ina` | |
| | CONFIG_CMD_JUMPTAGS | `jumptags` | |
| x | CONFIG_CMD_KEYBOARD | `8042`<br>`ksstate`<br>`kbpress` | Used by `firmware_ECKeyboard` |
| | CONFIG_CMD_LEDTEST | `ledtest` | |
| | CONFIG_CMD_MCDP | `mcdp` | |
| | CONFIG_CMD_MD | `md` | |
| | CONFIG_CMD_MEM | | Not a console command - gates `md` and `rw` |
| | CONFIG_CMD_MFALLOW | `mfallow` | |
| | CONFIG_CMD_MMAPINFO | `mmapinfo` | |
| x | CONFIG_CMD_PD | `pd` | Used by FAFT PD, TAST `firmware.ECSystemLocked` |
| | CONFIG_CMD_PD_DEV_DUMP_INFO | | Not supported by TCPMv2 |
| | CONFIG_CMD_PD_FLASH | `pd flash` | Not supported by TCPMv2 |
| | CONFIG_CMD_PD_SRCCAPS_REDUCED_SIZE | `pd <port> srccaps` | Defining this reduces the verbosity of this command, saving bytes |
| | CONFIG_CMD_PECI | `peci` | |
| | CONFIG_CMD_PLL | `pll` | only used by lm4 chip |
| | CONFIG_CMD_POWERINDEBUG | `powerindebug` | |
| | CONFIG_CMD_POWERLED | `powerled` | |
| x | CONFIG_CMD_POWER_AP | `apreset`<br>`apshutdown` | Used by power_Monitoring.py |
| | CONFIG_CMD_PPC_DUMP | `ppc_dump` | |
| | CONFIG_CMD_PS2 | `ps2ench`<br>`ps2write` | Used only on NPCX |
| | CONFIG_CMD_PWR_AVG | `pwr_avg` | |
| | CONFIG_CMD_RAND | `rand` | Used only on STM32 |
| | CONFIG_CMD_REGULATOR | `ir357x` | |
| | CONFIG_CMD_RESET_FLAGS | `rflags` | |
| | CONFIG_CMD_RETIMER | `retimer` | |
| | CONFIG_CMD_RTC | `rtc` | |
| | CONFIG_CMD_RTC_ALARM | `rtc_alarm` | |
| | CONFIG_CMD_RW | `rw` | |
| | CONFIG_CMD_SCRATCHPAD | `scratchpad` | |
| | CONFIG_CMD_SEVEN_SEG_DISPLAY | `seg` | |
| | CONFIG_CMD_SHA256_TEST | `???` | |
| x | CONFIG_CMD_SHMEM | `shmem` | Used by firmware_ECSharedMem |
| | CONFIG_CMD_SLEEPMASK | `sleepmask` | Only used for Cr50 tests |
| | CONFIG_CMD_SLEEPMASK_SET | | Adds options to `sleepmask` |
| | CONFIG_CMD_SPI_FLASH | `spi_flasherase`<br>`spi_flashwrite`<br>`spi_flashread`<br>`spi_flash_rsr`<br>`spi_flash_wsr`<br>`spi_flash_wsr` | |
| | CONFIG_CMD_SPI_NOR | `spinorinfo`<br>`spinorerase`<br>`spinorwrite`<br>`spinorread` | |
| | CONFIG_CMD_SPI_XFER | `spixfer` | |
| x | CONFIG_CMD_SYSINFO | `sysinfo` | Used by firmware_ECSystemLocked |
| x | CONFIG_CMD_SYSJUMP | `sysjump` | Used by firmware_ECSharedMem |
| | CONFIG_CMD_SYSLOCK | `syslock` | |
| | CONFIG_CMD_TASKREADY | `taskready` | |
| | CONFIG_CMD_TASK_RESET | `taskreset` | |
| | CONFIG_CMD_TCPC_DUMP | `tcpci_dump` | |
| x | CONFIG_CMD_TEMP_SENSOR | `temps` | |
| | CONFIG_CMD_TIMERINFO | `timerinfo` | |
| | CONFIG_CMD_TYPEC | `typec` | |
| | CONFIG_CMD_USART_INFO | `usart_info` | |
| | CONFIG_CMD_USB_PD_CABLE | `pdcable` | |
| x | CONFIG_CMD_USB_PD_PE | `pe` | Doesn't appear to be used but might be by FAFT PD |
| x | CONFIG_CMD_WAITMS | `waitms` | firmware_ECWatchdog | |

<!-- mdformat on -->

## Reduce or eliminate USB-C debugging

The TCPM (Type-C Port manager) implementation is one of the more complex modules
implemented by the EC code. This module includes extensive debugging and is
enabled by default due to the value provided during both board bringup and on
production systems.

The TCPM provides the following debug levels:

*   `DEBUG_DISABLE` (0) - Debugging disabled, no runtime messages displayed
*   `DEBUG_LEVEL_1` (1) - Displays all the state transitions for the TC (Type-C)
    and PE (Policy Engine) state machines
*   `DEBUG_LEVEL_2` (2) - Displays the raw contents of received PD (Power
    Delivery) packets, excluding PING packets
*   `DEBUG_LEVEL_3` (3) - Enables debug messages in the PRL Also displays
    received PING packets.

When `CONFIG_USB_PD_DEBUG_LEVEL` is undefined, the EC allows runtime
configuration of the USB-C debug level using the `pd dump <level>` EC console
command. In this configuration, the strings from all debug levels are included
in the image.

Enabling a fixed debug level removes runtime control of the debug level and also
removes the strings for the higher debug levels.

For cros-ec builds, add the following to your board.h/baseboard.h file:

```c
    #define CONFIG_USB_PD_DEBUG_LEVEL <level>
```

For zephyr-ec builds, add the following to your prj.conf file:

```
    CONFIG_PLATFORM_EC_USB_PD_DEBUG_FIXED_LEVEL=y
    CONFIG_PLATFORM_EC_USB_PD_DEBUG_LEVEL=<level>
```

Approximate flash space savings from each fixed level setting:

Fixed Debug Level | Relative Saving | Cumulative Saving
----------------- | --------------- | -----------------
Disabled          | 0               | 0
3                 | 100 bytes       | 100 bytes
2                 | 500-600 bytes   | 600-700 bytes
1                 | 100 bytes       | 700-800 bytes
0                 | 2000 bytes      | 2700-2800 bytes

The recommended setting is setting the fixed debug level to `DEBUG_LEVEL_2` (2).
This adds details about received PD packets in the EC log stored by the kernel
and can help troubleshoot PD issues when a PD analyzer isn't available.

It is not recommended to set the fixed debug level to `DEBUG_DISABLE` (0) on any
shipping firmware.

### TCPMv1 Configuration

Many older platforms still use the legacy TCPMv1 (`CONFIG_USB_PD_TCPMV1`)
implementation. Specific to TCPMv1, the PD protocol state names can be removed
from the debug output by adding the following to the board.h/baseboard.h file.

```c
#undef CONFIG_USB_PD_TCPMV1_DEBUG
```

This saves around 900 bytes of flash space. TCPMv2 does not currently provide an
equivalent configuration option, so there is also no Kconfig equivalent.

## Other optional features

### ASSERT() Calls

By default, `ASSERT()` calls generate a console message of the following form:

```
    ASSERTION FAILURE '<expr>' in function() at file:line
```

There are two options available that reduce the size of strings stored with the
`ASSERT()` calls.

<!-- mdformat off(Don't format table) -->

Description | cros-ec setting | zephyr-ec setting | Total Savings
:--- | :--- | :--- | :---
Display only file and line number | `#define CONFIG_DEBUG_ASSERT_BRIEF` | `CONFIG_PLATFORM_EC_DEBUG_ASSERT_BRIEF=y` | 2000-2500 bytes
Disable all debug from ASSERT() calls.<br> EC is reset using a software breakpoint. | `#undef CONFIG_DEBUG_ASSERT_REBOOTS` | `CONFIG_PLATFORM_EC_DEBUG_ASSERT_REBOOTS=n`<br>`CONFIG_PLATFORM_EC_DEBUG_ASSERT_BREAKPOINT=y` | 3000-4000 bytes

<!-- mdformat on -->

It is not recommended to disable `CONFIG_PLATFORM_EC_DEBUG_ASSERT_REBOOTS` on
shipping firmware.

### Disable console help and history

The help strings can be removed from the final build, saving about 5000 bytes of
flash space. The history command can also be disabled to save another 200 bytes
of flash space.

For cros-ec builds, add `#undef CONFIG_CONSOLE_CMDHELP` and `#undef
CONFIG_CONSOLE_HISTORY` to the board.h/baseboard.h file.

zephyr-ec builds use Zephyr's shell subsystem and by default enable the
`CONFIG_SHELL_MINIMAL` option. This option already disables shell help along
with many other non-critical features. Refer to the shell subsystem [Kconfig][2]
source file for the complete list of shell features than can be configured.

### Link time optimizaiton

Link time optimization (LTO) is a feature of the linker to identify and remove
unused code.

For cros-ec builds, LTO is enabled by adding this to the board.h/baseboard.h
file.

```c
#define CONFIG_LTO
```

For zephyr-ec builds, LTO is enabled by default and is controlled with Kconfig.

```
CONFIG_LTO=y
```

Note that for zephyr-ec builds, LTO is only turned on for the source files found
under `platform/ec`. The upstream Zephyr code does not currently support LTO due
to some auto-generated code that breaks the assumptions made by the linker. This
[Github issue][4] tracks the effort to support LTO in the Zephyr kernel.

### CONFIG_CHIP_INIT_ROM_REGION

The config option `CONFIG_CHIP_INIT_ROM_REGION` creates a new linker section to
store data that remains resident in ROM/flash at runtime. This reduces the
effective cros-ec image size by identifying data structures that do not need to
be copied into the code RAM section at startup.

This option has the following requirements:

1.  EC executes code from RAM
2.  The ROM/flash size is larger than 2 times the code RAM size.
3.  The RO code released for the board includes this
    [change](https://crrev.com/c/2428566).

The only EC chip that matches these prerequisites is the Nuvoton NPCX7.

Due to the RO code requirement, take care before enabling this option for boards
released prior to 2021.

If the above requirements are meant, add the following to the
board.h/baseboard.h file:

```c
#define CONFIG_CHIP_INIT_ROM_REGION
#define CONFIG_CHIP_DATA_IN_INIT_ROM
```

These options are not supported for zephyr-ec builds.

### Enable short GPIO names

The [GPIO macros](./configuration/gpio.md) defined by the board get stored as
descriptive strings for use with the `gpioget` and `gpioset` console commands.

The names of the GPIOs can be shorted by enabling the
`CONFIG_COMMON_GPIO_SHORTNAMES` option.

For example, the Kukui board defines this GPIO:

```c
GPIO(PMIC_FORCE_RESET_ODL, PIN(A, 2),  GPIO_ODR_HIGH)
```

Normally, the GPIO name is stored exactly as specified by the macro:
`PMIC_FORCE_RESET_ODL`. However, when `CONFIG_COMMON_GPIO_SHORTNAMES` is
defined, then the GPIO name is shortened to only include port and pin number:
`A2`.

This option is currently only supported by the STM32 chip and it is not
supported by zephyr-ec builds.

Note that there are some [FAFT tests][5] that rely on the GPIO name. If you
enable this option, you may also need to change firmware testing configuration
[file][6].

[1]:./zephyr/zephyr_build.md#Working-outside-the-chroot
[2]:https://github.com/zephyrproject-rtos/zephyr/blob/main/subsys/shell/Kconfig
[3]:https://docs.zephyrproject.org/latest/guides/optimizations/tools.html
[4]:https://github.com/zephyrproject-rtos/zephyr/issues/2112
[5]:https://chromium.googlesource.com/chromiumos/third_party/autotest/+/069cb4b0/server/site_tests/firmware_ECUsbPorts/firmware_ECUsbPorts.py#81
[6]:https://chromium.googlesource.com/chromiumos/platform/fw-testing-configs/+/e2e9547e/volteer.json#26
