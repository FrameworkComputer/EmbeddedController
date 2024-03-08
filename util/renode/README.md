# Renode

This directory holds the configuration files for Renode and this doc, which
provides a quick-start for Renode and EC.

## Installing Latest Renode

Outside of the chroot, on gLinux or Debian, please run the
`util/renode-deb-install.sh` script.

## Launching Renode

Outside of the chroot, you can use the `util/renode-ec-launch` script to start
Renode.

The script will utiize the optional `BOARD` and `PROJECT` environment variables
to adjust these respective EC parameters. The project parameter selects whether
you want to run the default "ec" firmware image, or whether you want to run a
unittest image, like "aes".

Here are some examples:

```bash
# Just launch bloonchipper normal ec image.
make BOARD=bloonchipper all
BOARD=bloonchipper ./util/renode-ec-launch
```

```bash
# Let's run the aes unittest image.
make BOARD=bloonchipper test-aes
BOARD=bloonchipper PROJECT=aes ./util/renode-ec-launch
```

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
