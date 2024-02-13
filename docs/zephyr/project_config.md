Project Configuration
=====================

[TOC]

## Overview

This document defines the organization of the program and project specific files
needed by Zephyr EC projects.

The goals of the project organization include:

* Minimize code duplication, allowing multiple projects to share common
  configuration options and devicetree nodes.
* Define the set of files required by each project.
* Define the best practices for devicetrees.


### Glossary

- **program**: The name of a Chromebook reference design. The **program**
  includes all Chromebooks based on a single AP SoC, such as Intel MeteorLake,
  Qualcomm 7c G3, or AMD Mendocino. The **program** corresponds to a single
  board overlay in the ChromeOS SDK.  The term *baseboard* is often used as a
  synonym for **program**.

- **project**: The name of a specific Chromebook model or variant.  All
  Chromebook **programs** contain at least one **project** which serves as the
  reference design(s) for the **program**. The reference **project** may or may
  not use the same name as the **program**. For example, the reference
  **project** for the skyrim **program** is also called skyrim. The corsola
  **program** included two reference **projects**, kingler and krabby. For the
  legacy ECOS builds, *board* was used as a synonym for **project**.

This document uses bold to highlight the terms **program** and **project** to
reference the definitions above.

## Directory Structure

The [`zephyr/program`](../../zephyr/program) contains the **program** and
**project** configuration files for all Zephyr based EC builds.

### `zephyr/program` directory

Each **program** has it's own subdirectory under `zephyr/program`.

```
zephyr/program/
├── brya/
├── corsola/
├── herobrine/
├── intelrvp/
├── it8xxx2_evb/
├── minimal/
├── nissa/
├── npcx_evb/
├── rex/
├── skyrim/
└── trogdor/
```

> The [`zephyr/program/minimal`](../../zephyr/program/minimal/) **program**
contains example EC projects that demonstrate how to build a Zephyr EC with the
minimum feature set enabled. These projects require only a working UART on the
target board.

### `zephyr/program/`**`<program>`**`/` Directory Overview

Each **program** subdirectory contains a subdirectory foreach each **project**,
including a subdirectory for the reference **project**.

The minimum configuration for a **program** named *skyrim* with just a single
reference **project**, also named *skyrim*, is shown below.

```
zephyr/program/skyrim/
├── include/
│   └── <program headers>.h
├── skyrim/
│   ├── include/
│   │   └── <project headers>.h
│   ├── src/
│   │   └── <project sources>.c
│   ├── CMakeLists.txt
│   ├── project.conf
│   └── project.overlay
├── src/
│   └── <program sources>.c
├── BUILD.py
├── CMakeLists.txt
├── Kconfig
├── program.conf
└── <devicetrees>.dtsi
```

#### `zephyr/program/`**`<program>`**`/` Directory Details

Description of the files and directories found directly in the **<program>**
level directory. Note that all paths are relative to the `zephyr/program/`
directory.

- **`<program>`**`/`: Top level directory for the **program**. [skyrim] is the
  *program* name in the example above.
- **`<program>`**`/include/`: Directory containing the header files common to
  all **projects** in the **program**. Use of **program** level includes is
  discouraged.  Instead, consider creating a generic driver that can be shared
  across all **programs**.
- **`<program>`**`/src/`: Directory containing the C source files common to all
  **projects** in the **program**.
- [**`<program>`**`/BUILD.py`](#build_py): Defines which **projects** can be
  made from this directory.
- [**`<program>`**`/CMakeLists.txt`](#cmakelists_txt): CMake file for the
  **program**.
- [**`<program>`**`/Kconfig`](#kconfig) - Defines new Kconfig options, used by
  all **projects** in the **program**.
- [**`<program>`**`/program.conf`](#program_conf) - Sets the default Kconfig
  settings for all **projects**.
- **`<program>`**`/<devictrees>.dtsi` - One or more devicetree files, organized
  by the hardware module or EC feature.  See the [Devicetree Best
  Practices](#devicetree-best-practices) section for additional information.
- **`<program>`**`/`**`<project>`**`/`: Top level directory for the
  **<project**>. Create a separate directory for each **project** defined by the
  **program**.

### `zephyr/program/`**`<program>`**`/`**`<project>`**`/` Directory Details

Each **project** provides the following files. Note that all paths are relative
to the `zephyr/program/`**`<program>`**`/` directory.

- **`<project>`**`/include/`: The **project** may optionally provide a public
  include directory, but this is discouraged. There are some exceptions where
  the legacy EC code expects the project to define a public header, such as the
  keyboard_customization.h file.
- **`<project>`**`/src/`: Directory containing the C source files specific to
  the **project**.
- [**`<project>`**`/CMakeLists.txt`](#cmakelists_txt): CMake file for the
  **project**.
- [**`<project>`**`/project.conf`](#project_conf): Kconfig settings for the
  **project**.
- [**`<project>`**`/project.overlay`](#project_overlay): Main devicetree overlay
  for the **project**.

Creation of custom C source files specific to the **program** or **project** is
discouaraged. You can usually project manage project specific settings with
Kconfig and devicetree changes only.

Note that **program** and **project** custom C files are still subject to the
same unit test requirements. So all custom C files also require that you write
tests.

## Setting up a new **program**

To set up a new EC **program**, create a new directory under
[`zephyr/program`] with the organization shown below.
Note that for this example, the new **program** is called "my_program", and the
reference **project** is called "my_reference_project".

> Tip - Copy one the **projects** defined by the [*minimal*] **program** to
> start with the bare miminimum of features required to boot the Zephyr EC
> appliation. Then follow the steps in the detailed in [Creating a New Zephyr EC
> Project].

```
zephyr/program/my_program/
├── my_reference_project/
│   ├── CMakeLists.txt
│   ├── project.conf
│   └── project.overlay
├── BUILD.py
├── CMakeLists.txt
├── Kconfig
└── program.conf
```

An in-depth example of each file is given below:

### BUILD.py

`BUILD.py` is a Python-based config file for setting up your reference
board and the associated variants.  The name `BUILD.py` is important
and case-sensitive: `zmake` searches for files by this
name.

When `BUILD.py` is sourced, the following two globals are defined:

- `here`: A `pathlib.Path` object containing the path to the directory
  `BUILD.py` is located in.
- `register_project`: A function which informs `zmake` of a new
  project to be built.  Your `BUILD.py` file needs to call this
  function one or more times.

`register_project` takes the following keyword arguments:

- `project_name` (required): The name of the project (typically the
  Google codename).  This name must be unique amongst all projects
  known to `zmake`, and `zmake` will error if you choose a conflicting
  name.
- `zephyr_board` (required): The name of the EC chip used. **Note:** the concept
  of a Zephyr board does not align with the ChromeOS concept of a board. The
  Zephyr build system requires a set of devicetree and Kconfig files under under
  `boards/google/${ZEPHYR_BOARD_NAME}`. For the Zephyr EC application, the EC
  chip is mapped onto the Zephyr board organization. Supported `zephyr_boards`
  include:
   - `mec172x/mec172x_nsz/mec1727`: Microchip MEC1727, 416 KiB RAM, 512 KiB flash
   - `npcx7`: Nuvoton NPCX7m7FC, 384 KiB, 512 KiB flash
   - `npcx9/npcx9m3f`: Nuvoton NPCX9m3F, 320 KiB RAM, 512 KiB flash
   - `npcx9/npcx9m7f`: Nuvoton NPCX9m7F, 384 KiB RAM, 1 MiB flash
   - `it8xxx2/it81202bx`: ITE IT81202, 60 KiB RAM, 1 MiB flash
   - `it8xxx2/it81302bx`: ITE IT81302, 60 KiB RAM, 1 MiB flash
   - other supported boards are defined in the project
   [`boards`](../../zephyr/boards) directory
- `supported_toolchains` (required): A list of the toolchain names
  supported by the build.  Valid values are:
  - `coreboot-sdk`: only supported in the chroot
  - `host`: used for unit and integration tests
  - `llvm`: only supported in the chroot
  - `zephyr`: only supported outside the chroot
- `output_packer` (required): An output packer type which defines
  which builds get generated, and how they get assembled together into
  a binary.
- `modules` (optional): A list of module names required by the
  project.  The default, if left unspecified, is to use all modules
  known by `zmake`.  Generally speaking, there is no harm to including
  unnecessary modules as modules are typically guarded by Kconfig
  options, so the only reason to set this is if your project needs to
  build in a limited environment where not all modules are available.
- `dts_overlays` (optional): A list of files which should be concatenated
  together and applied as a Zephyr device-tree overlay. The recommended setting
  is to select the **project** specific devicetree overlay file.

    ``` python
    dts_overlays=[here / project_name / "project.overlay"]
    ```

- `kconfig_files` (optional): A list of files that contain the Kconfig settings
  for the **project**. The recommended setting is select the **program**
  configuration file followed by the **project** configuration file.

    ``` python
    kconfig_files=[here / "program.conf", here / <project> / "project.conf",]
    ```

- `project_dir` (optional): The path to where `CMakeLists.txt` and
  `Kconfig` can be found for the project, defaulting to `here`.

Note that most projects will not want to call `register_project`
directly, but instead one of the helper functions, which sets even
more defaults for you:

- `register_host_project`: Define a project which runs in the chroot
  (not on hardware).
- `register_raw_project`: Register a project which builds a single
  `.bin` file, no RO+RW packing, no FMAP.
- `register_binman_project`: Register a project which builds RO and RW
  sections, packed together, and including FMAP.
- `register_npcx_project`: Just like `register_binman_project`, but
  expects a file generated named `zephyr.npcx.bin` for the RO section
  with Nuvoton's header.

You can find the implementation of these functions in
[`zephyr/zmake/zmake/configlib.py`](../../zephyr/zmake/zmake/configlib.py).

`BUILD.py` files are auto-formatted with `black`.  After editing a
`BUILD.py` file, please run `black BUILD.py` on it.

### CMakeLists.txt

This file, should at minimum contain the following:

``` cmake
cmake_minimum_required(VERSION 3.20.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(**project**)
```

If your **program** provides any C files, add them to your program CMake file
using `zephyr_library_sources()`.

``` cmake
zephyr_library_sources("src/my_program_source.c")
```

For your **project** C files, create **`<project>/`**`CMakeLists.txt` and use
`add_subdirectory()` to include the **project** CMake file.

``` cmake
add_subdirectory("my_reference_project")
```

Add the requires `zephyr_library_souces()` calls to the
**`<project>/`**`CMakeLists.txt` file.

If your **program** or **project** provides a public header, make the include
directory visible to rest of the code using
`cros_ec_library_include_directories()`.

``` cmake
cros_ec_library_include_directories("include")
```

### Kconfig

If certain projects need project-specific C files or ifdefs, the only
way to do so is to create a `Kconfig` file with the options schema you
want, and use it to toggle the inclusion of certain files.

The file must end with a single line that reads
`source "Kconfig.zephyr"`.  Note that this file is optional, so it's
recommended to only include it if you really need it.

### program.conf

`program.conf` has default Kconfig settings for all **projects** defined for the
**program**. The format is `KEY=VALUE`, as typical for Kconfig.

### project.conf

`project.conf` has the Kconfig settings for a single **project**. The format is
`KEY=VALUE`, as typical for Kconfig.

Kconfig settings in `project.conf` take precedence over the Kconfig settings
from `program.conf`.

### project.overlay

`project.overlay` is the main devicetree overlay for the **project**. The
`project.overlay` contains the following components:
- One or more `#include` statements to add devicetrees defined by the
  **program** into project.
- `/delete-node/` statements to remove specific devicetree nodes defined by the
  **program** devicetrees.
- New devicetree nodes for **project** specific settings that are not provided
  by any **program** devicetrees.

> Tip: After building your **project**, you can view the final devicetree in the
file `build/zephyr/`**`<project>`**`/build-ro/zephyr/zephyr.dts`.

## Setting up a new variant of an EC program

**Unlike our legacy EC, there are no files or directories to copy and
paste to setup a new variant in Zephyr code.**

Simply add a `register_project`-based call to the existing `BUILD.py`
for your reference board.

Below is an example of how programs may wish to structure this in
`BUILD.py`:

``` python
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def register_variant(project_name, chip="it8xx2", extra_dts_overlays=()):
    return register_binman_project(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[
            here / "base_power_sequence.dts",
            here / "i2c.dts",
            **extra_dts_overlays,
        ],
    )


# Reference board
register_variant(
    project_name="asurada",
    extra_dts_overlays=[here / "reference_gpios.dts"],
)

# Variants
register_variant(
    project_name="hayato",
    extra_dts_overlays=[here / "hayato_gpios.dts"],
)
```

If a project is going to be a simple variant of another project (e.g.,
project `bar` is exactly identical to project `foo` but has just a few
device-tree/Kconfig changes), you can spin a new variant using the
return value of the register functions:

``` python
foo = register_variant(project_name="foo")
bar = foo.variant(
    project_name="bar",
    dts_overlays=[here / "bar_extras.dts"],
)
```

With this simple variant syntax, lists (like Kconfig files and DTS
overlays) are concatenated.  This means it's not possible to remove
files during variant registration for this syntax, so it's only
recommended for the simple case.

## Devicetree Best Practices

Below are the best practices for devicetree organization:

* Split the devicetree across multiple files, organized by the functional block.
  This organization applies to the shared **program** devicetrees only.
  * FW_CONFIG
  * GPIOs
  * I2C
  * Interrupts
  * Keyboard
  * LEDs
  * Sensors
  * Thermal (fans and temperature sensors)
  * USB-C
* When creating **program**, usually with a single reference project, add the
  shared devicetree files in the program directory, separated by the functional
  area noted above.
* Each project creates a `project.overlay` file, and uses `#include` statements
  to add shared devicetree files from the program directory. An example
  project.overlay for the skyrim project is shown below.

  ``` c
  /* Copyright 2021 The ChromiumOS Authors
   * Use of this source code is governed by a BSD-style license that can be
   * found in the LICENSE file.
   */

  /* Skyrim program common DTS includes */
  #include "../adc.dtsi"
  #include "../fan.dtsi"
  #include "../gpio.dtsi"
  #include "../i2c.dtsi"
  #include "../interrupts.dtsi"
  #include "../keyboard.dtsi"
  #include "../motionsense.dtsi"
  #include "../usbc.dtsi"

  /* Skyrim project node overrides */
  /* ... */
  ```

### Managing **project** specific settings

When the **project** needs to make changes to the shared devicetree files, there
are two strategies:

* For small changes, add the `/delete-node/` attribute to the `project.overlay`
  file to remove the specific devices and devicetree nodes from the **project**
  final devicetree.
* For larger changes, remove the corresponding `#include` statements from the
  `project.overlay` file. Then,dDirectly add any required nodes to the
  `project.overlay` file.

In both cases, the shared devicetree file in the **program** directory is not
changed.

### Small Devicetree Changes

Examples of small devicetree changes include:
* Change the I2C peripheral address of a device.
* Changing USB-C related chips.
* Changing motionsense properties, such as the odr and ec-rate properties.
* Overriding a specific property of a node - for instance modifying the
  `pinctrl-0` property to adjust the EC pins connected to a device driver.

The example below demonstrates how to define a device in a **program**
devicetree file and then override the setting in the `project.overlay` file.

* The herobrine program defines the TCPC at I2C address `0xb` in the file
`zephyr/program/herobrine/i2c.dtsi`. This I2C address is valid for the
herobrine, evoker, and villager projects while the hoglin project needs to
change the I2C address to `0x1b`.

  ``` c
  /* zephyr/program/herobrine/i2c.dtsi */

  &i2c1_0 {
    status = "okay";
    /* ... */
    tcpc_port0: ps8xxx@b {
      compatible = "parade,ps8xxx";
      reg = <0xb>;
    };
  };
  ````
* The hoglin `project.overlay` file deletes the TCPC node at address `0xb` and
creates a new node at address `0x1b`.  The node name “tcpc_port0” is kept the
same, so any references to this node name do not change, such as the USB-C port
configuration.

  ``` c
  /* zephyr/program/herobrine/hoglin/project.overlay */

  #include “../i2c.dtsi”

  &i2c1_0 {
    /delete-node/ ps8xxx@b;
    tcpc_port0: ps8xxx@1b {
      compatible = "parade,ps8xxx";
      reg = <0x1b>;
    };
  };
  ```

While it is also possible to change a device’s I2C address by directly
overriding the `reg` property, this should not be done. Changing only the `reg`
property causes a mismatch between the node name, `ps8xxx@b`, and the actual
device address, `0x1b`.

Your `project.overlay` file can also directly override properties defined by the
**program** devicetree files.

* The skyrim **program** sets the I2C clock frequency for first I2C bus to fast
  (400 KHz).
  ``` c
  /* zephyr/program/skyrim/i2c.dtsi */
  &i2c0_0 {
    status = "okay";
    label = "I2C_TCPC0";
    clock-frequency = <I2C_BITRATE_FAST>;
    pinctrl-0 = <&i2c0_0_sda_scl_gpb4_b5>;
    pinctrl-names = "default";
  };
  ```
* Override the I2C clock frequency to fast-plus (1 MHz) in the winterhold
  `project.overlay`.
  ``` c
  /* zephyr/program/skyrim/winterhold/project.overlay */
  #include "../i2c.dtsi"
  &i2c0_0 {
    clock-frequency = <I2C_BITRATE_FAST_PLUS>;
  };
  ```
### Large Devicetree Changes

For large devicetree changes, the preference is to copy the relevant devicetree
fragment into the `project.overlay` file and edit the fragment directly.

Examples of large devicetree changes (or changes that don’t benefit from using
the /delete-node/ attribute) include:
* GPIOs - specifically the “named-gpios” node.  This integrates better with the
  arbitrage and the pinmap utility, which auto-generates the EC GPIO settings
  based on schematic data.
* Changes to the motionsense sensor types.  Currently x86 architectures impose a
  fixed ordering for the accelerometers and gryoscopes when accessed through the
  LPC memory map (see the [`EC_MEMMAP_ACC_DATA`]). Deleting nodes changes the
  order of the children under the motionsense-sense node and causes the test
  [`hardware.SensorAccel`] to fail. Copy the motionsense nodes into the
  project.overlay file and modify as required.
* LED policies - generally each OEM/ODM defines unique LED policies for their
  designs to establish differentiation for their brand. There is little value to
  creating common LED policies for all **projects** in the **program**.
* Batteries - batteries also are generally specific to the OEM/ODM. Define the
  **project** batteries directly in the `project.overlay` file.


[skyrim]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/platform/ec/zephyr/program/skyrim
[`zephyr/program`]: ../../zephyr/program/
[*minimal*]: ../../zephyr/program/minimal/
[Creating a New Zephyr EC Project]: ./zephyr_new_board_checklist.md
[`EC_MEMMAP_ACC_DATA`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/platform/ec/include/ec_commands.h;l=181
[`hardware.SensorAccel`]: https://crsrc.org/o/src/platform/tast-tests/src/chromiumos/tast/local/bundles/cros/hardware/sensor_accel.go;drc=8fbf2c53960bc8917a6a01fda5405cad7c17201e;l=30
