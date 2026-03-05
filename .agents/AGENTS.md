This document contains agent context for EC development.

# Overview

This repository contains the Zephyr EC (embedded controller) firmware for
Chromium OS devices. The EC handles low-level tasks such as power sequencing,
battery charging, keyboard control, thermal management, power delivery, etc.

Starting roughly in July of 2021, Chromebooks switched from the original Google
Chrome EC to an application based on the
[Zephyr Project RTOS](https://zephyrproject.org/).
The following terms are used to describe these two implementations:
* Original Chrome EC: ECOS, cros-ec, and legacy EC
* Zephyr-based EC: Zephyr EC

Legacy EC development has moved to the ec-legacy branch, also at
`chromiumos/src/platform/ec-legacy`.

This repository is focused on Zephyr EC; any legacy-only code is pending
removal. The Zephyr RTOS project is mirrored at
`chromiumos/src/third_party/zephyrproject/zephyr`.

See local `docs/` folder for detailed documentation on specific topics.

# Glossary and Terminology

@context/ec_terms.md

## General Terminology

*   **CBMEM**: Coreboot memory. Used to retrieve early boot/BIOS logs using the
    `cbmem` command on DUT host.
*   **CCD (Case Closed Debugging)**: A feature provided by the GSC that allows
    developers to access the EC console, AP console, and flash the EC.
*   **DUT**: Device Under Test. The device being tested or developed on. The DUT
    may be local or remote in lab.
*   **EC Console**: Interactive shell available via UART (Servo). Use `help` for
    commands. Use `dut-control ec_uart_pty` to find the EC's UART path.
*   **FAFT**: Fully Automated Firmware Tests. A collection of tests and related
    infrastructure that exercise and verify capabilities of Chrome OS. Run using
    `tast`.
*   **GBB (Google Binary Block)**: A read-only section of the AP firmware that
    stores keys and flags. GBB flags are often manipulated during development
    (using futility) to bypass developer mode screens, alter boot behavior, or
    disable Software Sync.
*   **GSC / Cr50 / Ti50**: Google Security Chip. A custom Titan-based secure
    microcontroller on the board. The EC communicates heavily with the GSC for
    TPM functionality, Case Closed Debugging (CCD), and power sequencing.
*   **Program** The program includes all Chromebooks based on a single AP SoC.
    The term baseboard is often used as a synonym.
*   **Project** The name of a specific variant of a program. The term board or
    model is often used as a synonym.
*   **Servo**: A physical hardware debug board connected to the device. It
    provides access to the EC's serial console (UART), hardware reset controls,
    and allows flashing the EC even if DUT is unresponsive.
*   **Tast**: The ChromeOS integration testing framework, frequently used to
    validate firmware behavior (e.g., `firmware.EcStress`).

# Tooling

*   **`cros_sdk` (chroot)**: The ChromiumOS build environment. Build and test
    commands must be run inside this chroot. Use `cros_sdk --working-dir . <cmd>`
    from the host to run commands inside.
*   **`dut-control`**: Command-line tool to interact with the Servo. It allows
    you to control the DUT (e.g., `dut-control cold_reset:on`), read GPIOs, etc.
    Use `dut-control -i` to list all available commands.
*   **`ectool`**: Host utility to send commands to the EC from userspace (run
    from the DUT host).
*   **`flash_ec`**: Utility script (`util/flash_ec`) to flash EC firmware via Servo.
*   **`futility`**: Used to manage GBB flags (e.g., to disable Software Sync
    for development) (run inside `cros_sdk` or on the DUT).
*   **`repo`**: The repository management tool built on top of Git, used to
    manage the massive ChromeOS source tree.
*   **`twister`**: Zephyr's standard test runner tool, used to run unit and
    integration tests for the Zephyr EC.
*   **`zmake`**: The build tool for Zephyr EC projects (run inside `cros_sdk`).

# Source Code Organization

Zephyr EC images rely on multiple Chromium repositories to build Zephyr EC
images.

* `chromiumos/src/third_party/zephyrproject/zephyr` - Google's local mirror of
  the main Zephyr RTOS source located on
  [Github](https://github.com/zephyrproject-rtos/zephyr).
* `chromiumos/src/platform/ec` - local repository containing the Zephyr EC
  application (this repository).

All paths below are relative to the Chrome EC base directory
`chromiumos/src/platform/ec`.

* `common/` - Upper-level code shared across boards and the EC implementations.
  This includes the following features:
  * Battery charging
  * USB Power Delivery
  * AP chipset power sequencing
  * Motionsense (EC sensor support)
  * Keyboard handling
  * Verified boot support

* `driver/` - Low-level drivers for on-board peripherals controlled by the EC.
  This does not include any drivers for modules integrated directly into the EC
  chipset (such as GPIO controllers, I2C, controllers, keyboard controller).
  On-board peripheral drivers include:
  * Charge controllers
  * USB Power Delivery chips (TCPCs, PPCs, muxes, etc)
  * Temperature sensors
  * Motionsense sensors (accelerometers, gyroscopes, light sensors, etc)

* `include/` - Header files for the `common/` and `driver/` code

* `build/` - The build output directory. This directory should be excluded when
  searching (e.g. grep --exclude-dir=build).

* `util/` - The EC utilities, including the `ectool` command-line tool.

The following legacy EC directories have been removed or are no longer used
by the Zephyr EC:
* `baseboard/`
* `board/`
* `chip/`
* `core/`

## Zephyr Subdirectory

The following provides an overview of the sub-directories found under
`chromiumos/src/platform/ec/zephyr/`.

* `app/` - The Zephyr EC application entry point. The Zephyr kernel
  calls `ec_app_main()` routine after initializing all kernel services and
  chip-level drivers,
* `boards/` - Contains the EC chip-level support. This directory and the
  organization is required by the Zephyr build system.  This should not be
  confused with the legacy EC `boards/` directory, as it does not contain any
  Chromebook specific board code.
* `cmake/` - Configuration files for the CMake build system.
* `drivers/` - Drivers conforming to the Zephyr device model to
  implement Chrome EC specific features. Google plans to eventually move these
  drivers upstream.
* `dts/` - Devicetree description files for Google's Zephyr drivers that
  are not yet available upstream.
* `emul/` - Emulator source code that has not yet been moved upstream.
* `include/` - Header files for files found in the `zephyr/`
  sub-directory.
* `linker/` - Linker directive files used to construct the Zephyr EC
  binaries.
* `program/` - Program-specific configurations for each program
  supported by Zephyr.
* `shim/` - Source code that adapts the legacy EC APIs to the equivalent
  Zephyr OS API.
* `subsys/` - Staging area for subsystem code that will be moved
  upstream.
* `test/` - Host based emulation tests.
* `zmake/` - Source code for the `zmake/` meta tool.

# Building

To build the EC for a single project, run `zmake build <project>` in the
chroot:

For example, to build the EC for `skyrim`, run:

```
cros_sdk --working-dir . -- zmake build skyrim
```

The output binary will then be located at `build/zephyr/skyrim/output/ec.bin`.

Additional output files:

*   `build/zephyr/skyrim/output/zephyr.ro.elf` - read-only ELF for debugging
*   `build/zephyr/skyrim/output/zephyr.rw.elf` - read-write ELF for debugging
*   `build/zephyr/skyrim/build-rw/zephyr/.config` - Kconfig options selected
*   `build/zephyr/skyrim/build-rw/zephyr/include/generated/devicetree_unfixed.h`
    - the (large) header file that zephyr uses to provide devicetree
    information to the C code
*   `build/zephyr/skyrim/build-rw/zephyr/zephyr.dts` - devicetree that is used
*   `build/zephyr/skyrim/build-rw/zephyr/zephyr.map` - map of image

# Testing

Zephyr EC unit tests are based on ztest and run using the `twister` tool. See
`docs/zephyr/ztest.md` for more information.

## Running twister

### Run all tests under a specific directory

```
platform/ec$ ./twister -T path/to/my/tests
```

### Run a specific test
```
platform/ec$ ./twister -s <test dir>/<my.test.scenario>
```

For example:
```
platform/ec$ ./twister -s drivers/drivers.default
```

`drivers/` is the directory under `zephyr/test/` that contains the requested
test, and `drivers.default` is the specific test scenario specified in that
directory's `testcase.yaml` file.

### Run all tests with coverage

```
./twister -p native_sim -p unit_testing --coverage
```

### Get more info on twister
```
./twister --help
```

# Development Workflow

* **VCS**: Use `repo` outside chroot. `repo start <branch>` and
  `repo upload . --cbr`. Gerrit is used for code review.
* **Coding Style**:
  [Linux Kernel style guide](https://www.kernel.org/doc/html/latest/process/coding-style.html).
  Use `cros format <file>` to format code.
* **Commit Messages**: Must include `BUG=b:<id>` and `TEST=<description>`. Keep
  each line under 80 characters. `Change-Id` must be preserved. See previous
  commits for examples.
* **Upstream First**: Changes to the Zephyr RTOS should be submitted to the
  Zephyr GitHub project first.
* **Small Changes**: Prefer smaller logical changes that solve a single problem.
  When changing common code shared between boards along with board specific
  code, split the shared code change into its own change list (CL). The board
  specific CL can depend on the shared code CL.
* **Validate Changes**: Always validate changes by running unit tests and
  building for all relevant boards. If possible run validation tests on an
  actual device and tast integration tests.
* **Code Coverage**: All changes must have at least 90% unit test coverage.
* **Firmware Branches**: Some programs use program specific firmware release
  branches. Changes are first applied to main and then cherry-picked to the
  program specific firmware branches.

# Meta Guidelines

*   **No Sensitive Data**: Check for sensitive or private data before
    submitting.
*   **Public Only**: This repository is public. Internal-only context, skills,
    or commands MUST be added to the internal `cros-ec-development` extension
    instead.
