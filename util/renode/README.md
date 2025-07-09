# Renode

This directory holds the configuration files for Renode and this doc, which
provides a quick-start for Renode and EC.

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

The [`renode-ec-launch`] script is a convenient wrapper to configure and
run Renode for specific boards. It works both inside and outside the chroot and
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
