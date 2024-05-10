# Embedded Controller (EC)

[TOC]

> **Note** - This document covers the legacy Chrome EC implementation. The
> legacy EC implementation is used by all Chromebook reference designs prior to
> July 2021. On newer Chromebook designs, the EC implementation is based on the
> Zephyr RTOS. Refer to the [Zephyr EC Introduction](./docs/zephyr/README.md)
> for details on the Zephyr EC implementation.

## Introduction

The Chromium OS project includes open source software for embedded controllers
(EC) used in recent ARM and x86 based Chromebooks. This software includes a
lightweight, multitasking OS with modules for power sequencing, keyboard
control, thermal control, battery charging, and verified boot. The EC software
is written in C and supports
[a variety of micro-controllers](https://chromium.googlesource.com/chromiumos/platform/ec/+/main/chip/).

This document is a guide to help make you familiar with the EC code, current
features, and the process for submitting code patches.

For more see the Chrome OS Embedded Controller
[presentation](https://docs.google.com/presentation/d/1Xa_Z6SjW-soPvkugAR8__TEJFrJpzoZUa9HNR14Sjs8/pub?start=false&loop=false&delayms=3000)
and [video](http://youtu.be/Ie7LRGgCXC8) from the
[2014 Firmware Summit](http://dev.chromium.org/chromium-os/2014-firmware-summit).

## What you will need

1.  A Chromebook with a compatible EC. This includes the Samsung Chromebook
    (XE303C12) and all Chromebooks shipped after the Chromebook Pixel 2013
    (inclusive). See the
    [Chrome OS devices](http://dev.chromium.org/chromium-os/developer-information-for-chrome-os-devices)
    page for a list.
1.  A Linux development environment. The latest Debian Stable (x86_64) is commonly used.
    Linux in a VM may work if you have a powerful host machine.
1.  A [servo debug board](http://dev.chromium.org/chromium-os/servo) (and
    header) is highly recommended for serial console and JTAG access to the EC.
1.  A sense of adventure!

## Terminology

### EC

EC (aka Embedded Controller) can refer to many things in the Chrome OS
documentation due to historical reasons. If you just see the term "EC", it
probably refers to "the" EC (i.e. the first one that existed). Most Chrome OS
devices have an MCU, known as "the EC" that controls lots of things (key
presses, turning the AP on/off). The OS that was written for "the" EC is now
running on several different MCUs on Chrome OS devices with various tweaks (e.g.
the FPMCU, the touchpad one that can do palm rejection, etc.). It's quite
confusing, so try to be specific and use terms like FPMCU to distinguish the
fingerprint MCU from "the EC".

See the [EC Acronyms and Technologies](./docs/ec_terms.md) for a more complete
glossary.

## Getting the EC code

The code for the EC is open source and is included in the Chromium OS
development environment (`~/trunk/src/platform/ec/</code>`).
See[ http://www.chromium.org/chromium-os/quick-start-guide](http://dev.chromium.org/chromium-os/quick-start-guide)
for build setup instructions. If you want instant gratification, you can fetch
the source code directly. However, you will need the tool-chain provided by the
Chromium OS development environment to build a binary.

```bash
git clone https://chromium.googlesource.com/chromiumos/platform/ec
```

The source code can also be browsed on the web at:

https://chromium.googlesource.com/chromiumos/platform/ec/

## Code Overview

The following is a quick overview of the top-level directories in the EC
repository:

**baseboard** - Code and configuration details shared by a collection of board
variants. Tightly linked with the `board` directory described below.

**board** - Board specific code and configuration details. This includes the
GPIO map, battery parameters, and set of tasks to run for the device.

**build** - Build artifacts are generated here. Be sure to delete this and
rebuild when switching branches and before "emerging" (see Building an EC binary
below). make clobber is a convenient way to clean up before building.

**chip** - IC specific code for interfacing with registers and hardware blocks
(adc, jtag, pwm, uart etc…)

**core** - Lower level code for task and memory management.

**common** - A mix of upper-level code that is shared across boards. This
includes the charge state machine, fan control, and the keyboard driver (among
other things).

**driver** - Low-level drivers for light sensors, charge controllers,
I2C/onewire LED controllers, and I2C temperature sensors.

**include** - Header files for core and common code.

**util** - Host utilities and scripts for flashing the EC. Also includes
“ectool” used to query and send commands to the EC from userspace.

**test** - Unit tests for EC components. These can be run locally in a mock
"host" environment or compiled for a target board. If building for a target
board, the test must be flashed and run manually on the device. All unit tests
and fuzzers are build/run using the local host environment during a `buildall`.
To run all unit tests locally, run `make runhosttests -j`. To build a specific
unit test for a specific board, run `make test-<test_name> BOARD=<board_name>`.
Please contribute new tests if writing new functionality. Please run `make help`
for more detail.

**fuzz** - Fuzzers for EC components. These fuzzers are expected to run in the
mock host environment. They follow the same rules as unit tests, as thus use the
same commands to build and run.

## Firmware Branches

Each Chrome device has a firmware branch created when the read-only firmware is
locked down prior to launch. This is done so that updates can be made to the
read-write firmware with a minimal set of changes from the read-only. Some
Chrome devices only have build targets on firmware branches and not on
cros/main. Run “`git branch -a | grep firmware`” to locate the firmware branch
for your board. Note that for devices still under development, the board
configuration may be on the branch for the platform reference board.

To build EC firmware on a branch, just check it out and build it:

```bash
git checkout cros/firmware-falco_peppy-4389.B
```

To make changes on a branch without creating a whole new development environment
(chroot), create a local tracking branch:

```bash
git branch --track firmware-falco_peppy-4389.B cros/firmware-falco_peppy-4389.B

git checkout firmware-falco_peppy-4389.B

make clobber

# <make changes, test, and commit them>

repo upload --cbr .

# (The --cbr means "upload to the current branch")
```

Here is a useful command to see commit differences between branches (change the
branch1...branch2 as needed):

```bash
git log --left-right --graph --cherry-pick --oneline branch1...branch2
```

For example, to see the difference between cros/main and the HEAD of the current
branch:

```bash
git log --left-right --graph --cherry-pick --oneline cros/main...HEAD

# Note: Use three dots “...” or it won’t work!
```

## Building an EC binary

Note: The EC is normally built from within the Chromium OS development chroot to
use the correct toolchain.

Building directly from the EC repository:

```bash
cros_sdk
cd ~/chromiumos/src/platform/ec
make -j BOARD=<boardname>
```

Where `<boardname>` is replaced by the name of the board you want to build an
EC binary for. For example, the boardname for the Chromebook Pixel is “link”.
The make command will generate an EC binary at `build/<boardname>/ec.bin`. The
`-j` tells make to build multi-threaded which can be much faster on a multi-core
machine.

### Building via emerge (the build file used when you build Chrome OS):

(optional) Run this command if you want to build from local source instead of
the most recent stable version:

```bash
cros_workon-<boardname> start chromeos-ec
```

Build the EC binary:

```
emerge-<boardname> chromeos-ec
```

Please be careful if doing both local `make`s and running emerge. The emerge can
pick up build artifacts from the build subdirectory. It’s best to delete the
build directory before running emerge with `make clobber`.

The generated EC binary from emerge is found at:

```
(chroot) $ /build/<boardname>/firmware/ec.bin
```

or

```
(chroot) $ /build/<boardname>/firmware/<devicename>/ec.bin
```

The `devicename` is the name of a device (also referred as board or variant) which belongs to a family of baseboard. `boardname` is the baseboard name in this case. Example : `/build/dedede/firmware/madoo/ec.bin`

The ebuild file used by Chromium OS is found
[here](https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/main/chromeos-base/chromeos-ec/chromeos-ec-9999.ebuild):

```bash
(chroot) $ ~/trunk/src/third_party/chromiumos-overlay/chromeos-base/chromeos-ec/chromeos-ec-9999.ebuild
```

## Flashing an EC binary to a board

### Flashing via the servo debug board

If you get an error, you may not have set up the dependencies for servo
correctly. The EC (on current Chromebooks) must be powered either by external
power or a charged battery for re-flashing to succeed. You can re-flash via
servo even if your existing firmware is bad.

```bash
(chroot) $ sudo emerge openocd
```

```bash
(chroot) $ ~/trunk/src/platform/ec/util/flash_ec --board=<boardname> [--image=<path/to/ec.bin>]
```

Note: This command will fail if write protect is enabled.

If you build your own EC firmware with the `make BOARD=<boardname>` command the
firmware image will be at:

```bash
(chroot) $ ~/trunk/src/platform/ec/build/<boardname>/ec.bin
```

If you build Chrome OS with `build_packages` the firmware image will be at:

```bash
(chroot) $ /build/<boardname>/firmware/ec.bin
```

or

```
(chroot) $ /build/<boardname>/firmware/<devicename>/ec.bin
```

The `devicename` is the name of a device (also referred as board or variant) which belongs to a family of baseboard. `boardname` is the baseboard name in this case. Example : `/build/dedede/firmware/madoo/ec.bin`

Specifying `--image` is optional. If you leave off the `--image` argument, the
`flash_ec` script will first look for a locally built `ec.bin` followed by one
generated by `emerge`.

### Flashing on-device via flashrom

Assuming your devices boots, you can flash it using the `flashrom` utility. Copy
your binary to the device and run:

```bash
(chroot) $ flashrom -p ec -w <path-to/ec.bin>
```

Note: `-p internal:bus=lpc` also works on x86 boards...but why would you want to
remember and type all that?

## Preventing the RW EC firmware from being overwritten by Software Sync at boot

A feature called "Software Sync" keeps a copy of the read-write (RW) EC firmware
in the RW part of the system firmware image. At boot, if the RW EC firmware
doesn't match the copy in the system firmware, the EC’s RW section is
re-flashed. While this is great for normal use as it makes updating the EC and
system firmware a unified operation, it can be a challenge for EC firmware
development. To disable software sync a flag can be set in the system firmware.
Run the following commands from a shell on the device to disable Software Sync
and turn on other developer-friendly flags (note that write protect must be
disabled for this to work):

```bash
# futility gbb --set --flash --flags=0x239
```

```bash
# reboot
```

This turns on the following flags:

*   `GBB_FLAG_DEV_SCREEN_SHORT_DELAY`
*   `GBB_FLAG_FORCE_DEV_SWITCH_ON`
*   `GBB_FLAG_FORCE_DEV_BOOT_USB`
*   `GBB_FLAG_DISABLE_FW_ROLLBACK_CHECK`
*   `GBB_FLAG_DISABLE_EC_SOFTWARE_SYNC`


Alternatively, if the OS cannot be accessed, the same flag can be set over a servo with:

```bash
$ sudo futility gbb -s --flags=0x239 --servo
```

The `GBB` (Google Binary Block) flags are defined in the
[vboot_reference source](https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/main/firmware/2lib/include/2struct.h).
A varying subset of these flags are implemented and/or relevant for any
particular board.

## Using the EC serial console

The EC has an interactive serial console available only through the UART
connected via servo. This console is essential to developing and debugging the
EC.

Find the serial device of the ec console (on your workstation):

```bash
(chroot) $ dut-control ec_uart_pty
```

Connect to the console:

```bash
(chroot) $ socat READLINE /dev/pts/XX
```

Where `XX` is the device number. Use `cu`, `minicom`, or `screen` if you prefer
them over `socat`.

### Useful EC console commands:

**help** - get a list of commands. help <command> to get help on a specific
command.

**chan** - limit logging message to specific tasks (channels). Useful if you’re
looking for a specific error or warning and don’t want spam from other tasks.

**battfake** - Override the reported battery charge percentage. Good for testing
low battery conditions (LED behavior for example). Set “battfake -1” to go back
to the actual value.

**fanduty** - Override automatic fan control. “fanduty 0” turns the fan off.
“autofan” switches back to automated control.

**hcdebug** - Display the commands that the host sends to the EC, in varying
levels of detail (see include/ec_commands.h for the data structures).

## Host commands

The way in which messages are exchanged between the AP and EC is
[documented separately](./docs/ap-ec-comm.md).

## Software Features

### Tasks

Most code run on the EC after initialization is run in the context of a task
(with the rest in interrupt handlers). Each task has a fixed stack size and
there is no heap (malloc). All variable storage must be explicitly declared at
build-time. The EC (and system) will reboot if any task has a stack overflow.
Tasks typically have a top-level loop with a call to task_wait_event() or
crec_usleep() to set a delay in uSec before continuing. A watchdog will trigger
if a task runs for too long. The watchdog timeout varies by EC chip and the
clock speed the EC is running at.

The list of tasks for a board is specified in ec.tasklist in the `board/$BOARD/`
sub-directory. Tasks are listed in priority order with the lowest priority task
listed first. A task runs until it exits its main function or puts itself to
sleep. The highest priority task that wants to run is scheduled next. Tasks can
be preempted at any time by an interrupt and resumed after the handler is
finished.

The console `taskinfo` command will print run-time stats on each task:

```
> taskinfo
Task Ready Name         Events      Time (s)  StkUsed
   0 R << idle >>       00000000   32.975554  196/256
   1 R HOOKS            00000000    0.007835  192/488
   2   VBOOTHASH        00000000    0.042818  392/488
   3   POWERLED         00000000    0.000096  120/256
   4   CHARGER          00000000    0.029050  392/488
   5   CHIPSET          00000000    0.017558  400/488
   6   HOSTCMD          00000000    0.379277  328/488
   7 R CONSOLE          00000000    0.042050  348/640
   8   KEYSCAN          00000000    0.002988  292/488
```

The `StkUsed` column reports the largest size the stack for each task grew since
reset (or sysjump).

### Hooks

Hooks allow you to register a function to be run when specific events occur;
such as the host suspending or external power being applied:

```
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change_callback, HOOK_PRIO_DEFAULT);
```

Registered functions are run in the HOOKS task. Registered functions are called
in priority order if more than one callback needs to be run. There are also
hooks for running functions periodically: `HOOK_TICK` (fires every
`HOOK_TICK_INVERVAL` ms which varies by EC chip) and `HOOK_SECOND`. See
hook_type in
[include/hooks.h](https://chromium.googlesource.com/chromiumos/platform/ec/+/main/include/hooks.h)
for a complete list.

### Deferred Functions

Deferred functions allow you to call a function after a delay specified in uSec
without blocking. Deferred functions run in the HOOKS task. Here is an example
of an interrupt handler. The deferred function allows the handler itself to be
lightweight. Delaying the deferred call by 30 mSec also allows the interrupt to
be debounced.

```
static int debounced_gpio_state;

static void some_interrupt_deferred(void)
{

        int gpio_state = gpio_get_level(GPIO_SOME_SIGNAL);

        if (gpio_state == debounced_gpio_state)
                return;

        debounced_gpio_state = gpio_state;

        dispense_sandwich(); /* Or some other useful action. */
}

/* A function must be explicitly declared as being deferrable. */
DECLARE_DEFERRED(some_interrupt_deferred);

void some_interrupt(enum gpio_signal signal)
{
        hook_call_deferred(some_interrupt_deferred, 30 * MSEC);
}
```

### Shared Memory Buffer

While there is no heap, there is a shared memory buffer that can be borrowed
temporarily (ideally before a context switch). The size of the buffer depends on
the EC chip being used. The buffer can only be used by one task at a time. See
[common/shared_mem.c](https://chromium.googlesource.com/chromiumos/platform/ec/+/main/common/shared_mem.c)
for more information. At present (May 2014), this buffer is only used by debug
commands.

## Making Code Changes

If you see a bug or want to make an improvement to the EC code please file an
issue at [crbug.com/new](http://crbug.com/new). It's best to discuss the change
you want to make first on an issue report to make sure the EC maintainers are
on-board before digging into the fun part (writing code).

In general, make more, smaller changes that solve single problems rather than
bigger changes that solve multiple problems. Smaller changes are easier and
faster to review. When changing common code shared between boards along with
board specific code, please split the shared code change into its own change
list (CL). The board specific CL can depend on the shared code CL.

### Coding style

The EC code follows the
[Linux Kernel style guide](https://www.kernel.org/doc/html/latest/process/coding-style.html).
Please adopt the same style used in the existing code. Use tabs, not spaces, 80
column lines etc...

Other style notes:

1.  Globals should either be `static` or `const`. Use them for persistent state
    within a file or for constant data (such as the GPIO list in board.c). Do
    not use globals to pass information between modules without accessors. For
    module scope, accessors are not needed.
1.  If you add a new `#define` config option to the code, please document it in
    [include/config.h](https://chromium.googlesource.com/chromiumos/platform/ec/+/main/include/config.h)
    with an `#undef` statement and descriptive comment.
1.  The Chromium copyright header must be included at the top of new files in
    all contributions to the Chromium project:

    ```
    /* Copyright <year> The ChromiumOS Authors
     * Use of this source code is governed by a BSD-style license that can be
     * found in the LICENSE file.
     */
    ```

### Submitting changes

Prior to uploading a new change for review, please run the EC unit tests with:

```bash
(chroot) $ make -j buildall
```

```bash
(chroot) $ make -j runtests
```

These commands will build and run unit tests on your host.

Pre-upload checks are run when you try to upload a change-list. If you wish to
run these checks manually first, commit your change locally then run the
following command from within the chroot and while in the `src/platform/ec`
directory:

```bash
(chroot) $ ~/trunk/src/repohooks/pre-upload.py
```

The pre-upload checks include checking the commit message.  Commit messages must
have a `BUG` and `TEST`.  You may also optionally include a `BRANCH` line with a
list of board names for which the CL should be cherry-picked back to old
branches for.

Please refer to existing commits (`git log`) to see the proper format for the
commit message.

Note that code you submit must adhere to the [ChromeOS EC Firmware Test
Requirements].

## Debugging

While adding `printf` statements can be handy, there are some other options for
debugging problems during development.

### Serial Console

There may already be a message on the serial console that indicates your
problem. If you don’t have a servo connected, the `ectool console` command will
show the current contents of the console buffer (the buffer’s size varies by EC
chip). This log persists across warm resets of the host but is cleared if the EC
resets. The `ectool console` command will only work when the EC is not write
protected.

If you have interactive access to the serial console via servo, you can use the
read word `rw` and write word `ww` commands to peek and poke the EC's RAM. You
may need to refer to the datasheet for your EC chip or the disassembled code to
find the memory address you need. There are other handy commands on the serial
console to read temperatures, view the state of tasks (taskinfo) which may help.
Type `help` for a list.

### Panicinfo

The EC may save panic data which persists across resets. Use the “`ectool
panicinfo`” command or console “`panicinfo`” command to view the saved data:

```
Saved panic data: (NEW)
=== HANDLER EXCEPTION: 05 ====== xPSR: 6100001e ===
r0 :00000001 r1 :00000f15 r2 :4003800c r3 :000000ff
r4 :ffffffed r5 :00000799 r6 :0000f370 r7 :00000000
r8 :00000001 r9 :00000003 r10:20002fe0 r11:00000000
r12:00000008 sp :20000fd8 lr :000012e1 pc :0000105e
```

The most interesting information are the program counter (`pc`) and the link
register (return address, `lr`) as they give you an indication of what code the
EC was running when the panic occurred. `HANDLER EXCEPTIONS` indicate the panic
occurred while servicing an interrupt. `PROCESS EXCEPTIONS` occur in regular
tasks. If you see “Imprecise data bus error” listed, the program counter value
is incorrect as the panic occurred when flushing a write buffer. If using a
cortex-m based EC, add `CONFIG_DEBUG_DISABLE_WRITE_BUFFER` to your board.h to
disable write buffering (with a performance hit) to get a “Precise bus error”
with an accurate program counter value.

### Assembly Code

If you have a program counter address you need to make sense of, you can
generate the assembly code for the EC by checking out the code at the matching
commit for your binary (`ectool version`) and running:

```bash
(chroot) $ make BOARD=$board dis
```

This outputs two files with assembly code:

```
build/$board/RO/ec.RO.dis
build/$board/RW/ec.RW.dis
```

which (in the case of the LM4 and STM32) are essentially the same, but the RW
addresses are offset.

## Write Protect

See [Firmware Write Protection].

## EC Version Strings

The read-only and read-write sections of the EC firmware each have a version
string. This string tells you the branch and last change at which the firmware
was built. On a running machine, run `ectool version` from a shell to see
version information:

```
RO version:    peppy_v1.5.103-7abb4f7
RW version:    peppy_v1.5.129-cd1a1e9
Firmware copy: RW
Build info:    peppy_v1.5.129-cd1a1e9 2014-03-07 17:18:27 @build120-m2
```

You can also run the `version` command on the EC serial console for a similar
output.

The format of the version string is:

```
<board>_<branch number>.<number of commits since the branch tag was created>-<git hash of most recent change>
```

If the version is: `rambi_v1.6.68-a6608c8`:

*   board name = rambi
*   branch number = v1.6 (which is for the firmware-rambi branch)
*   number of commits on this branch (since the tag was added) = 68
*   latest git hash = a6608c8

The branch numbers (as of May 2014) are:

*   v1.0.0 cros/main
*   v1.1.0 cros/main
*   v1.2.0 cros/firmware-link-2695.2.B
*   v1.3.0 cros/firmware-snow-2695.90.B
*   v1.4.0 cros/firmware-skate-3824.129.B
*   v1.5.0 cros/firmware-4389.71.B
*   v1.6.0 cros/firmware-rambi-5216.B

Hack command to check the branch tags:

```
git tag

for hash in $(git for-each-ref --format='%(objectname)' refs/tags/); do
    git branch -a --contains $hash | head -1;
done
```

(If anyone can come up with something prettier, make a CL).

Run `util/getversion.sh` to see the current version string. The board name is
passed as an environment variable `BOARD`:

```bash
(chroot) $ BOARD="cheese" ./util/getversion.sh
```

```
cheese_v1.1.1755-4da9520
```

[Firmware Write Protection]: ./docs/write_protection.md

## CQ builder

To test the cq builder script run these commands:

### firmware-ec-cq
```
rm -rf /tmp/artifact_bundles /tmp/artifact_bundle_metadata \
  ~/chromiumos/src/platform/ec/build
./firmware_builder.py --metrics /tmp/metrics_build build && \
./firmware_builder.py --metrics /tmp/metrics_test test && \
./firmware_builder.py --metrics /tmp/metrics_bundle bundle && \
echo PASSED
cat /tmp/artifact_bundle_metadata
cat /tmp/metrics_build
ls -l /tmp/artifact_bundles/
```

[ChromeOS EC Firmware Test Requirements]: ./docs/chromeos-ec-firmware-test-requirements.md
