[TOC]

# Objective

Allow a subset of the platform/ec code to be built as part of the Zephyr-based
EC without needing to land code into upstream zephyr, or our zephyr-chrome
repository.

# Background

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

# Design ideas

In order to reuse `platform/ec` development , we shim "active" `platform/ec`
code as a
[Zephyr Module](https://docs.zephyrproject.org/latest/guides/modules.html). This
requires us to add some Zephyr specific code in a `zephyr` directory in the
`platform/ec` codebase. Once we release a Zephyr-based EC, then we can turn down
platform/ec for future development and work on migrating the platform/ec-module
code out of the module directory and into a first-class Zephyr code format -- in
the local
[Chrome Zephyr repo](https://chromium.googlesource.com/chromiumos/platform/zephyr-chrome/+/refs/heads/master)
and ultimately [upstream](https://github.com/zephyrproject-rtos/zephyr).

For platform/ec code that is stable and not under active development, the Zephyr
team may port that code to Zephyr, thus skipping the shimming process.

## Subsystems of interest

### With Shim

We shim the following subsystems (non-exhaustive).

*   USB-C: TCPC, PPC, MUX, TCPMv2
*   Charge Manager
*   SoC Power Sequencing
*   Sensors, if Intel’s HID-based solution is delayed in getting to Zephyr
    upstream

### Little-to-No Shim

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

## New content in platform/ec

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
        [`DECLARE_HOST_COMMAND`](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/platform/ec/include/host_command.h;l=256;drc=514923bc59f5a3435dbb7cbf348735ed41889ffe)
        to map to Zephyr's upstream
        [`EC_HOST_CMD_HANDLER`](https://github.com/zephyrproject-rtos/zephyr/blob/d7468bf836b75c29980441f294a61eae6bf4bc75/include/ec_host_cmd.h#L73)
        macro. This allows us to compile select platform/ec files in the Zephyr
        build.

## Namespace Collisions

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

## New CQ check

As long as code from platform/ec is part of the zephyr
[ebuild](http://cs/chromeos_public/src/third_party/chromiumos-overlay/chromeos-base/chromeos-zephyr-2_3/chromeos-zephyr-2_3-9999.ebuild),
then we need to run the Zephyr CQ checks on any platform/ec CLs in addition to
the normal platform/ec CQ checks. This ensures that platform/ec changes aren’t
breaking the Zephyr builds and requiring the Zephyr team to debug recent
changes.

For local builds, we can run `emerge-betty chromeos-zephyr-2_3` or `zmake`
utility to check that an EC CL has not broken anything on the Zephyr side.

We will work with the CI team to enable this.

# Alternatives Considered

## Translate code and mirror into the zephyr-chrome repository

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

## Don't do any code sharing

One option is to avoid shimming in any platform/ec code and allow the Zephyr
team to re-implement features in upstream zephyr, or our local zephyr-chrome
repository.

Disregarding the infeasible amount of work required to complete this option, the
platform/ec repository has a far faster development pace as there are many more
contributors, and the Zephyr features would quickly lose parity during the time
frame that we are launching both Zephyr-based and platform/ec-based devices.
