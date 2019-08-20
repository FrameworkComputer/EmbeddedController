# Creating a new EC board

[TOC]

## Overview

This document describes the high-level steps needed to create a new EC board. If
you're creating a new board based on existing baseboard, you can jump straight
to the relevant link found under [Configuring EC
Features](#Configure-EC-Features) and focus on known board changes.

## Conventions
### Key Files
Before you get started, it's important to understand the role of a few key files
in the EC codebase.

- [`include/config.h`](../include/config.h) {#config_h} - Contains the list of
  top-level configuration options for the Chrome EC codebase. Each configuration
  option is documented inline and is considered the authoritative definition.

- `baseboard/<name>/` - This directory contains header files and source files
  shared by all boards in a baseboard family.
    - `baseboard.h` - Contains the EC configuration options shared by all
      devices in the baseboard family.
    - `baseboard.c` - Contains code shared by all devices in the baseboard
      family.
    - `build.mk` - The board family makefile specifies C source files compiled
      into all boards in the baseboard family.

- `board/<board>` - Files in this directory are only built for a single board.
    - `board.h` - EC configuration options specific to a single board.
    - `board.c` - Code built only on this board.
    - `build.mk` {#board_build_mk} - The board makefile defines the EC chipset family, defines the
      baseboard name, and specifies the C source files that are compiled.
    - `gpio.inc` - This C header file defines the interrupts, GPIOs, and
      alternate function selection for all pins on the EC chipset.
    - `ec.tasklist` - This C header defines the lists of tasks that are enabled
      on the board.  See the main EC documentation more details on [EC tasks].

### GPIO Naming
Many drivers and libraries in the common EC code rely on board variants defining
an exact GPIO signal name. Examples include the `GPIO_LID_OPEN`,
`GPIO_ENTERING_RW`, and `GPIO_SYS_RESET_L` signals. The net names in schematics
often do not match these names exactly. When this occurs, best practice is that
all the `GPIO_INT()`, `GPIO()`, `ALTERNATE()`, and `UNIMPLEMENTED()` definitions
in `gpio.inc` use the schematic net name. You then create `#define` macros in
`board.h` to map the net names to the EC common names.

Below is an example configuration for the SYS_RESET_L signal.  The schematic net
name of this signal is EC_RST_ODL and the signal connects to the EC chipset pin
GPIO02.

```c
/* From gpio.inc */
GPIO(EC_RST_ODL,  PIN(0, 2), GPIO_ODR_HIGH)

/* From board.h */
/* Map the schematic net name to the required EC name */
#define GPIO_SYS_RESET_L  GPIO_EC_RST_ODL
```

Please see the [GPIO](./configuration/gpio.md) documentation for additional
details on the GPIO macros.

## How to use this document
Each of the following sections details a single feature set that may need to be
modified or configured for your new board. The feature sets are organized so
they can be implemented with a reasonably sized change list, and can be worked
on independently.

Each configuration feature document includes the following sub-tasks:

- **Config Options** - This section details the `CONFIG_*` options relevant to
  the feature. Use the documentation found in [config.h] to determine whether
  each option should be enabled (using #define) or disabled (using #undef) in
  the relevant `baseboard.h` or `board.h` file.
- **Feature Parameters** - This section details parameters that control the
  operation of the feature. Similar to the config options, feature parameters
  are defined in [config.h] and prefixed with `CONFIG_*`.  However, feature
  parameters are assigned a default value, which can be overridden in by
  `baseboard.h` or `board.h` using an `#undef/#define` pair.
  ```c
  #undef CONFIG_UART_TX_BUF_SIZE
  #define CONFIG_UART_TX_BUF_SIZE 4096
  ```
- **GPIOs and Alternate Pins** - This section details signals and pins relevant
  to the feature. Add the required `GPIO_INT()`, `GPIO()`, `ALTERNATE()`, and
  `UNIMPLEMENTED()` definitions to `gpio.inc`, making sure to follow the [GPIO
  naming conventions].
- **Data Structures** - This section details the data structures required to
  configure the feature for correct operation. Add the data structures to
  `baseboard.c` or `board.c`. Note that most data structures required by the
  common EC code should be declared `const` to save on RAM usage.
- **Tasks** - This section details the tasks that the EC feature requires for
  operation.
- **Testing and Debugging** - This section details strategies for testing the EC
  feature set and for debugging issues. This section also documents EC console
  commands related to the feature set.
- **Example** - When present, this section walks through a complete example for
  configuring an EC feature based on an existing board implementation.

## Create the new EC board

The first step when creating a new EC board, is to create the required files in
the `./baseboard` and `./board` directories. When adding a new board for an
existing baseboard family, use the python script [new_variant.py] to
automatically copy the `./board` directory from an existing EC board to get you
started. The [new_variant.py] script performs additional operations not directly
related to the EC code, including copying coreboot files and modifying the yaml
files. If you want to copy the EC board files only, you can directly call the
[create_initial_ec_image.sh] script. The instructions for running this script
are found in the corresponding [README.md] documentation.

The [new_variant.py] script also verifies the new EC board compiles and prepares
a changelist to upload to Gerrit. You should upload this changelist unmodified
for review and submission (you may need to run `make buildall -k` to satisfy
the EC pre-submit tests).

The next step is to review the following sections to make any needed
modifications to your new board files, test the changes, and upload the changes
for review.

### Creating a new reference board

If you are creating a new reference board, it is recommended that you manually
create new directories under the `./baseboard` and `./board` directories and
populate these directories with the minimum set of files required compile the EC
board. The initial changelists for the Hatch and Volteer reference boards
provide good examples for how to start.

  * [Volteer EC skeleton build]
  * [Hatch EC skeleton build]

After submitting the skeleton builds, review the following sections and add each
feature set as required by your design.

## Configure EC Features

The checklist below provides an overview of EC features that must be configured
for correct operation of a Chromebook. The "Needed for Power On" column
indicates which features are critical for board bringup. These features take
priority and should be ready before the first prototypes arrive. Use the
documentation link for details about the code changes required to implement each
feature.

| EC Feature | Needed for Power On |
| :--------- | ------------------: |
| [Configure EC Chipset](./configuration/ec_chipset.md) | yes |
| [Configure AP to EC Communication](./configuration/config_ap_to_ec_comm.md) | yes |
| [Configure AP Power Sequencing](./configuration/ap_power_sequencing.md) | yes |
| [Configure USB-C](./usb-c.md) | yes |
| [Configure Charger (TODO)](./configuration/template.md) | yes |
| [Configure I2C Buses](./configuration/i2c.md) | no |
| [Configure CrOS Board Information (CBI)](./configuration/cbi.md) | no |
| [Configure Keyboard](./configuration/keyboard.md) | no |
| [Configure LEDs](./configuration/leds.md) | no |
| [Configure Motion Sensors (TODO)](./configuration/motion_sensors.md) | no |
| [Configure BC1.2 Charger Detector (TODO)](./configuration/template.md) | no |
| [Configure Battery (TODO)](./configuration/template.md) | no |

After finishing the changes required for all EC features, it is recommended that
you make one final pass over all the GPIOs and pin assignments used on your
board. Refer to the [GPIO](./configuration/gpio.md) documentation for details.

[README.md]:https://chromium.googlesource.com/chromiumos/platform/dev-util/+/master/contrib/variant/README.md
[new_variant.py]:https://chromium.googlesource.com/chromiumos/platform/dev-util/+/master/contrib/variant/new_variant.py
[create_initial_ec_image.sh]:https://chromium.googlesource.com/chromiumos/platform/dev-util/+/master/contrib/variant/create_initial_ec_image.sh
[Volteer EC skeleton build]:https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/1758532
[Hatch EC skeleton build]:https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/1377569/
[config.h]: ./new_board_checklist.md#config_h
[EC tasks]: ../README.md#Tasks
[GPIO naming conventions]: ./new_board_checklist.md#GPIO-Naming