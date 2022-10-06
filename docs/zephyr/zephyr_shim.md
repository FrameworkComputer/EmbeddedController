[TOC]

# Zephyr Shimming How-To

## Objective

Allow a subset of the platform/ec code to be built as part of the Zephyr-based
EC without needing to land code into upstream zephyr, or our zephyr-chrome
repository.

## Background

Now that Google has joined [Zephyr OS](https://www.zephyrproject.org/), the EC
team is moving toward it instead of platform/ec code on embedded controllers for
future Chrome OS devices. See the
[originally proposed idea](https://goto.google.com/cros-ec-rtos) and a more
specific [Zephyr process doc](https://goto.google.com/zephyr-structure) of what
future development on Zephyr will look like.

Simply put, we want to move to Zephyr OS to use an open-source embedded OS that
has a vibrant community. The embedded OS scene for the industry is very
fragmented, with various parties using their own custom solution. We see the
strong open-source community at Zephyr as potentially helping to consolidate
efforts in the embedded controller space. It will also benefit our partners
(both chip vendors and OEMs) since they should only have to support one embedded
OS (i.e., Zephyr) for both their Chrome OS and Windows based devices.

Migrating to use Zephyr fully is going to take a few years. We do not want to
diverge from the active development happening on platform/ec code. We
potentially want to ship a product using Zephyr before the migration is
complete.

## Design ideas

In order to reuse `platform/ec` development , we shim "active" `platform/ec`
code as a
[Zephyr Module](https://docs.zephyrproject.org/latest/guides/modules.html). This
requires us to add some Zephyr specific code in a `zephyr` directory in the
`platform/ec` codebase. Once we release a Zephyr-based EC, then we can turn down
platform/ec for future development and work on migrating the platform/ec-module
code out of the module directory and into a first-class Zephyr code format -- in
the local
[Chrome Zephyr repo](https://chromium.googlesource.com/chromiumos/platform/zephyr-chrome/+/HEAD)
and ultimately [upstream](https://github.com/zephyrproject-rtos/zephyr).

For platform/ec code that is stable and not under active development, the Zephyr
team may port that code to Zephyr, thus skipping the shimming process.

### Subsystems of interest

#### With Shim

We shim the following subsystems (non-exhaustive).

*   USB-C: TCPC, PPC, MUX, TCPMv2
*   Charge Manager
*   SoC Power Sequencing
*   Sensors, if Intel’s HID-based solution is delayed in getting to Zephyr
    upstream

#### Little-to-No Shim

We adopt upstream Zephyr or skip the shimming process (non-exhaustive).

*   CBI and dependent EEPROM code
    *   The format is stable. We pull in the list of CBI tags from platform/ec
        though
*   EFS2, Vboot, RO/RW split
    *   Adjusting flash layout would be difficult to shim, and the concept is
        very stable.
    *   We may shim some core EFS2 logic
*   Host command framework
*   Sensors, if Intel’s HID-based solution getts to Zephyr upstream and passes
    CTS
*   Keyboard and keycode scanning support
    *   We may shim the newer Vivaldi feature.
*   Console support
    *   We allow individual console commands via DECLARE\_CONSOLE\_COMMAND to be
        shimmed to Zephyr. These convert commands to work with Zephyr's shell
        subsystem.
*   I2C

### New content in platform/ec

Add the `src/platform/ec/zephyr` folder with:

*   [Module integration files](https://docs.zephyrproject.org/latest/guides/modules.html#build-system-integration),
    e.g., module.yml, CMakeLists.txt, and KConfig.
    *   **module.yml** is the required entry point (must be located at
        _zephyr/module.yml_ in the repository) for Zephyr modules, and declares
        the location of Kconfig and CMakeLists.txt files used by the Zephyr
        build system.
    *   **CMakeLists.txt** replicates build logic for the files being shimmed,
        outside of the platform/ec Makefile.
    *   **Kconfig** will declare any CONFIG\_\* options which are important to
        expose from platform/ec code to the Zephyr build.
*   Shim code to translate platform/ec code into Zephyr code
    *   For example, redefine platform/ec’s
        [`DECLARE_HOST_COMMAND`](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/platform/ec/include/host_command.h;l=256;drc=514923bc59f5a3435dbb7cbf348735ed41889ffe)
        to map to Zephyr's upstream
        [`EC_HOST_CMD_HANDLER`](https://github.com/zephyrproject-rtos/zephyr/blob/d7468bf836b75c29980441f294a61eae6bf4bc75/include/ec_host_cmd.h#L73)
        macro. This allows us to compile select platform/ec files in the Zephyr
        build.

### Namespace Collisions

One significant issue of mixing Zephyr headers with our existing EC code is that
we currently have many names colliding with the Zephyr code. For example,
Zephyr's atomic functions also are named `atomic_add`, `atomic_or`, ...,
however, have a different API from our EC's atomic functions. This is critical,
since atomic operations are often used in `static inline` functions placed in
header files.

In some cases, we are able to hack around these collisions by creating macros
and functions which are compatible with both Zephyr and our EC's usages. For
example, we can create a modified `IS_ENABLED` which accepts both defined to
nothing usages (CrOS EC `config.h` style), and defined to `1` usages (Zephyr
Kconfig style).

However, long term, we may find this to be a continual cause of issues, and
creating hacks for every colliding macro or function may be unsustainable. We
propose _gradually_ introducing a namespace prefix to the `platform/ec`
codebase, for example `crec_`. We can begin at the critical areas of namespace
collision (e.g., atomics) and continue to improve the naming convention with
time.

### New CQ check

As long as code from platform/ec is part of the zephyr
[ebuild](http://cs/chromeos_public/src/third_party/chromiumos-overlay/chromeos-base/chromeos-zephyr-2_3/chromeos-zephyr-2_3-9999.ebuild),
then we need to run the Zephyr CQ checks on any platform/ec CLs in addition to
the normal platform/ec CQ checks. This ensures that platform/ec changes aren’t
breaking the Zephyr builds and requiring the Zephyr team to debug recent
changes.

For local builds, we can run `emerge-betty chromeos-zephyr-2_3` or `zmake`
utility to check that an EC CL has not broken anything on the Zephyr side.

We will work with the CI team to enable this.

## How to shim features

Before you get started shimming a feature, it's important to
understand the general philosophies behind Zephyr OS and shimming:

* Our current EC's OS isn't going away any time soon. Even after we
  ship our first device with a Zephyr-based EC, we may still be working on
  other projects using the old OS.  It's important to consider how
  your feature will apply to both the Zephyr world and CrOS EC OS
  world.

* We won't be converting old devices to use Zephyr-based firmware.
  This means that our existing OS and its code will need maintained
  for bug and security fixes for years to come.  **Do not allow the
  code you write for the CrOS EC OS to lack in quality or be "throw
  away code" as it will need to be maintained for a long time.**

* Shimming, by the very nature of the design, will lead to some ugly
  hacks.  We try and avoid this when we can, but some of them may be
  completely unavoidable.  This means we need to actively work against
  nature to keep the code clean.  If we do things right, there's even
  a possibility that we leave things cleaner than we found them.

* Shimming occasionally digs up landmines.  Be prepared to step on
  them. 💣

### What code can be shimmed?

Code in the `common/` directory (and other common code directories,
like `power/`) is the ideal target for shimming, with the exception of
core OS features which have Zephyr OS equivalents.

Code in the following directories should **never be shimmed**:

- `core/`: this directory contains architecture-specific code which
  should have a Zephyr OS equivalent feature.

- `chip/`: this directory contains chip-specific code, and similarly
  should have a Zephyr OS equivalent feature.

In both cases, you should instead determine (or, in rare cases,
implement upstream) the equivalent Zephyr OS feature, and *implement
an architecture and chip agnostic* "shim layer" in the `zephyr/shim/`
directory which translates the APIs as necessary.

As of the time of this document, the shim layer is nearing 100%
complete, and it should be rare that you encounter an API which needs
translation.

Finally, code in the following directories should **avoid being
shimmed, if possible**:

- `board/`: this directory contains variant-specific code.

- `baseboard/`: this directory contains baseboard-specific code.

In both cases, the only value in shimming in code from one of those
directories would be to enable a Zephyr OS build for a device which
already has CrOS EC OS support, as *Zephyr-only projects will not have
these directories*.  You should be thinking about how this would be
implemented for a Zephyr-only project, and filing bugs to create the
appropriate device-tree and Kconfig equivalents before shimming this
code.

See [Zephyr PoC device bringup](zephyr_poc_device_bringup.md) for more
information about bringing up proof-of-concept devices.

### Configuration

CrOS EC OS uses a special header `config.h`, which sets configuration
defaults, and includes board and chip specific configurations by
expecting the headers `board.h` and `config_chip.h` to be present.
Most of these configuration options start with `CONFIG_`, however the
rules were loosely defined over the years.

Zephyr OS, on the other hand, uses two different configuration
systems:

* Kconfig, the configuration system from the Linux Kernel, which
  fits well within the domain of preprocessor definitions in C.  The
  schema for our Kconfig files can be found under `zephyr/Kconfig`,
  and project-specific configurations are made in `prj.conf` files.

  Kconfig is generally used to select which EC software features are
  enabled, and should be avoided for hardware configurations, such as
  chip configuration registers and their default settings.

* Open Firmware Device Tree, which you may also be familiar with from
  the Linux kernel.  This configuration can be found in `*.dts` files.

  Device-tree is generally used for hardware configurations, and
  should be avoided for EC software feature configuration.

For code which is shimmed, we need to play nicely with both the CrOS
EC OS configuration system, and Zephyr's configuration systems.  Thus,
we follow the following pattern:

* EC software features are configured using `Kconfig` and
  `zephyr/shim/include/config_chip.h` translates them into the
  appropriate CrOS EC OS configurations using patterns such as below:

  ```c
  #undef CONFIG_CMD_GETTIME
  #ifdef CONFIG_PLATFORM_EC_TIMER_CMD_GETTIME
  #define CONFIG_CMD_GETTIME
  #endif
  ```

  The preprocessor code should follow that template exactly, and not
  use any nesting (Kconfig handles dependencies, there is no reason to
  do it again in the preprocessor code).

* **The domain of Kconfig options and CrOS EC configuration names
  should be completely distinct.**  This is because the Kconfig options
  are included automatically, and including `config.h` may undefine
  them.  To mitigate this, we follow a convention of using
  `CONFIG_PLATFORM_EC_` as the prefix for EC software features in
  Kconfig.

One special configuration option exists, `CONFIG_ZEPHYR`, which you
can use to detect whether the OS is Zephyr OS.  This is the
conventional way to add Zephyr-specific (or excluded) code in CrOS EC
code.

The typical EC macros for reducing `#ifdef` messes (e.g.,
`IS_ENABLED`, `STATIC_IF`, etc.) work with both CrOS EC OS and Kconfig
options, and should be used when possible.

### Header Files

Besides the include paths provided by Zephyr OS, the following paths
are additionally added for shimmed code:

* `include/`
* `zephyr/include/`
* `zephyr/shim/include/`

The names of headers in these directories should be completely
distinct.  C compilers have no mechanism for "include ordering", and
there is no way to "override a header".

If you feel the need to "override" a header, say `foo.h` in
`include/`, the best way to do this is to give it a different name
under `zephyr/shim/include` (e.g., `zephyr_foo_shim.h`), and include
that in the `foo.h` header with a `#ifdef CONFIG_ZEPHYR` guard.

The typical styling convention for includes (following existing
conventions in `platform/ec` and other C codebases we have) is:

* Zephyr OS headers in pointy brackets, in alphabetical order.

* One blank line

* CrOS EC OS headers (either from `include/`, `zephyr/shim/include/`,
  or the current directory), in quotes (not pointy brackets).

### Adding files to Cmake

Zephyr's build system (including shimmed code) uses CMake instead of
`Makefiles`, and your code will not be compiled for Zephyr unless you
list the files in `zephyr/CMakeLists.txt`.

### Step-by-step guide to adding a Kconfig

Follow these steps:

1. Make sure you have read the above Configuration section

2. Add your config to one of zephyr/Kconfig* files. Note the PLATFORM_EC_ prefix
   and try to put it near related things:

  ```kconfig
   config PLATFORM_EC_CHARGER_BQ25720
     bool "TI BQ25720 charger"
     help
       The TI BQ25720 is a blah blah (describe summary from datasheet,
       at least 3 lines so take 10 minutes to write something truly useful)
  ```

   Consider a `depends on PLATFORM_EC_...` line if it depends on an existing
   feature.

3. Add to zephyr/shim/include/config_chip.h (put it at the bottom):

  ```kconfig
   #undef CONFIG_CHARGER_BQ25720
   #ifdef CONFIG_PLATFORM_EC_CHARGER_BQ25720
   #define CONFIG_CHARGER_BQ25720
   #endif
  ```

4. Add the source file to zephyr/CMakeLists.txt if it is not already there. For
   ordering check the comments in that file:

   `zephyr_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_BQ25720
                                     "${PLATFORM_EC}/driver/charger/bq25720.c")`

5. Run a build on a board that enables the new CONFIG (in config.h) to make sure
   there are no problems.

6. If it doesn't work, please email zephyr-task-force@ or file a bug and assign
   it to sjg@, cc zephyr-task-force@ (please include CL link and the error
   output).

### Unit Tests

Unit tests, implemented using the Ztest framework, can be found in
`zephyr/test`.

To build all projects and run all unit tests, you use `zmake test --all`.

## Alternatives Considered

### Translate code and mirror into the zephyr-chrome repository

We could potentially write a script which, via a series of find/replace
operations, translates a platform/ec module to use Zephyr functions, macros, and
paradigms. On a frequent basis, we would translate all modules of interest in
the platform/ec repository and land an "uprev" change in the zephyr-chrome
repository.

The main disadvantage of this technique is that we can't get any CQ coverage
when platform/ec CLs land that the modules will continue to work in Zephyr.
Additionally, the translator script would be delicate and probably require
frequent maintenance.

However, this technique does have some benefits. With modules directly
translated to code in the Zephyr paradigm, the process of upstreaming a shimmed
module to ZephyrOS would be significantly easier. Additionally, it would require
no shim code in platform/ec.

### Don't do any code sharing

One option is to avoid shimming in any platform/ec code and allow the Zephyr
team to re-implement features in upstream zephyr, or our local zephyr-chrome
repository.

Disregarding the infeasible amount of work required to complete this option, the
platform/ec repository has a far faster development pace as there are many more
contributors, and the Zephyr features would quickly lose parity during the time
frame that we are launching both Zephyr-based and platform/ec-based devices.
