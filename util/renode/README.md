# Renode

This directory holds the configuration files for Renode and this doc, which
provides a quick-start for Renode and EC.

[TOC]

## Installing Renode

The ChromeOS chroot has a [`renode` ebuild] that is considered the "stable"
version in ChromeOS. You can install `renode` inside the chroot with:

```bash
(chroot) $ sudo emerge renode
```

Alternatively, you can download a prebuilt version of the `renode` ebuild using
[CIPD]. The following command will download the latest prebuilt version into a
directory called `renode`:

```bash
(chroot) $ echo "chromiumos/infra/tools/renode latest" | cipd ensure -ensure-file - --root renode
```

Note that the prebuilt version is not automatically in your `PATH`.

[`renode` ebuild]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/chromiumos-overlay/app-emulation/renode/
[CIPD]: http://go/luci-cipd

### Latest Version

Before updating the [`renode` ebuild] to a new version or to test out a bug fix
in Renode, you may want to use the latest nightly version of Renode. You can
either download and extract the tarball or use the `.deb` package.

#### Tarball

```bash
(inside/outside) $ wget https://builds.renode.io/renode-latest.linux-portable.tar.gz
```

This version works both inside and outside the chroot, but is not automatically
in your `PATH`.

#### Debian package

```bash
(outside) $ ./util/renode-deb-install.sh
```

## Launching Renode

The [`renode-ec-launch`] script is a convenient wrapper to configure and run
Renode for specific boards. It works both inside and outside the chroot and
configures the console as `/tmp/renode-uart`.

The script lets you run both EC and Zephyr images, including the "default" image
or a unit test image. For complete details, refer to the `--help` output.

### Examples

```bash
# Build bloonchipper EC image.
(chroot) $ make BOARD=bloonchipper -j
# Run the image in Renode.
(chroot) $ ./util/renode-ec-launch -b bloonchipper
# Connect to the console.
(chroot) $ screen /tmp/renode-uart
```

```bash
# Build the AES unit test image.
(chroot) $ make BOARD=bloonchipper test-aes -j
# Run the unit test image in Renode.
(chroot) $ ./util/renode-ec-launch -b bloonchipper --ec aes
# Connect to the console.
(chroot) $ screen /tmp/renode-uart
# Run the test from the console.
> runtest
```

```bash
# Build the bloonchipper Zephyr image.
(chroot) $ zmake build bloonchipper
# Run the image in Renode.
(chroot) $ ./util/renode-ec-launch -b bloonchipper --zephyr
# Connect to the console.
(chroot) $ screen /tmp/renode-uart
```

[`renode-ec-launch`]: ../renode-ec-launch

## Connecting GDB to Renode

### Setup

The easiest way to configure GDB and connect is to use the `util/gdbinit`. You
can configure your ec directory to always load this GDB init file by doing the
following outside your chroot:

```bash
ln -s util/gdbinit .gdbinit
# You need to allow GDB to auto load .gdbinit files in the ~/chromiumos dir.
echo 'add-auto-load-safe-path ~/chromiumos' >~/.gdbinit
```

Additionally, you will need a gdb version capable of debugging our armv7
binaries. On gLinux, you can install and use the `gdb-multiarch` package. Do the
following outside the chroot:

```bash
sudo apt install gdb-multiarch
```

### Launch and Connect

Like the Renode launch script, the EC gdbinit will looks for the `BOARD` and
`PROJECT` environment variables, when gdb starts up. Simple set the environment
variable and launch `gdb-multiarch`.

Here are some examples:

```bash
# Just debug bloonchipper normal ec image.
BOARD=bloonchipper gdb-multiarch
(gdb) connect
```

```bash
# Let's debug the aes unittest image.
BOARD=bloonchipper PROJECT=aes gdb-multiarch
(gdb) connect
```

For more details, please see the comments in [`util/gdbinit`](../gdbinit),
[`util/gdbinit.py`](../gdbinit.py), and
[`util/renode-ec-launch`](../renode_ec_launch.py).

For help with GDB, you can checkout the
[GDB Manual](https://sourceware.org/gdb/current/onlinedocs/gdb.html/).

## Hardware WP

You can type the following into the renode console to enable/disable HW GPIO:

Action            | Renode command for `bloonchipper`
----------------- | ----------------------------------
**Enable HW-WP**  | `sysbus.gpioPortB.GPIO_WP Release`
**Disable HW-WP** | `sysbus.gpioPortB.GPIO_WP Press`

Note, you can just type `sysbus`, `sysbus.gpioPortB`, or
`sysbus.gpioPortB.GPIO_WP` to learn more about these modules and the available
functions.

## Updating Renode in the chroot and CQ

To update the version of Renode used in the chroot and the CQ:

1.  **Update the `renode` ebuild:** Update the [`app-emulation/renode` ebuild].
    You'll need to rename the ebuild file to the new version, copy the source to
    [`localmirror`], and update the `Manifest` file. See the `TEST` lines in the
    example CL below.

    *   Example CL: https://crrev.com/c/7563809

2.  **Wait for the builder:** Once the ebuild update is merged, the
    `build-chromiumos-sdk-subtools` builder will build the new package and
    upload it to CIPD. This builder runs on a nightly basis; you can also
    manually trigger a build with the scheduler.

    *   **Scheduler:** [build-chromiumos-sdk-subtools]
    *   **Builder:** [infra/build-chromiumos-sdk-subtools]

    You can check for the new version in CIPD: [chromiumos/infra/tools/renode]

3.  **Update `firmware_builder.py`:** Update the `cipd_renode_version` variable
    in [`util/renode/firmware_builder.py`] with the new version string from CIPD
    (e.g., `ebuild_source:app-emulation/renode-1.16.0_p20260209,...`).

    *   Example CL: https://crrev.com/c/7568663

[build-chromiumos-sdk-subtools]: https://luci-scheduler.appspot.com/jobs/chromeos/build-chromiumos-sdk-subtools
[infra/build-chromiumos-sdk-subtools]: https://ci.chromium.org/p/chromeos/builders/infra/build-chromiumos-sdk-subtools
[chromiumos/infra/tools/renode]: https://chrome-infra-packages.appspot.com/p/chromiumos/infra/tools/renode
[`util/renode/firmware_builder.py`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/renode/firmware_builder.py
[`app-emulation/renode` ebuild]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/chromiumos-overlay/app-emulation/renode/
[`localmirror`]: https://www.chromium.org/chromium-os/developer-library/reference/third-party/archive-mirrors/

## Highlights

With Renode, we've found and prevented several bugs. Here are a few highlights:

*   [Toolchain uprev that Renode CQ prevented from breaking EC](https://issuetracker.google.com/409027503)
*   [Upstream Zephyr change that Renode CQ prevented from breaking EC](http://b/389761200)
*   [jump data pointer not initialized](http://b/291940520)
*   [`libcxx` change that broke EC](http://b/363082822)

It is also
[easier for non-firmware developers to debug issues](http://b/363082822#comment12),
since they don't need hardware.
