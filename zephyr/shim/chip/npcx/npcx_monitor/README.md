# NPCX Monitor

The code in this directory builds the `npcx_monitor.bin` binary. The NPCX
monitor executes from RAM and provides routines for erasing and programming
the internal flash.

> **Note** - If you make changes to the NPCX monitor, you must uprev the
> `npcx_monitor.bin` file used in the servo dockerfile.  See instructions below.


## Updating the servo dockerfile

Whenever you make changes to the NPCX monitor, you must manually update the
version installed into the servo dockerfile.

1. Build and test your NPCX monitor changes.
    * Note that you will need to update the SHA256 hash of the
    `npcx_monitor.bin` in the [`check_hash.cmake`] script.
    * Run `sha256sum npcx_monitor.bin` to determine the new value.
1. Upload a CL with your NPCX monitor changes for review.
1. Add the current date to the new `npcx_monitor.bin` filename. For example
  `npcx_monitor-2025.02.07.bin`.
1. Upload the renamed monitor file to Google Cloud Storage (GCS).
    * Use the `gsutil` command to upload the file.
    * Note that the name of the file must be unique.  The `-n` option prevents
      overwriting an existing file.
    * The `-a public-read` option makes the file publicly readable.
```bash
    gsutil cp -n -a public-read npcx_monitor-2025.02.07.bin \
        gs://chromeos-localmirror/distfiles/cros_ec/npcx_monitor/
```

1. Edit the NPCX monitor binary installed by the [servo dockerfile]
    * Find the instruction that fetches the NPCX monitor from GCS
    * Modify the `wget` instruction to point to the new monitor file uploaded
      above.
    * Upload a CL to the `third_party/hdctools` repository with your dockerfile
      change.

## Build instructions

### Building with a Zephyr EC project

Historically, the `npcx_monitor.bin` count have run-time differences based on
the EC project configuration. The NPCX monitor is automatically built with
every Zephyr EC project that uses an NCPX EC chip and the monitor file is
one of the generated artifacts.

After building the Zephyr EC project, the monitor file is located in the build
output directory.

```bash
./build/zephyr/<board>/output/npcx_monitor.bin
```

### Building standalone
The NPCX monitor firmware has now been made generic and works will all
NPCX chip types and configurations.

You can build just the NPCX monitor using the `npcx_monitor` zmake project.

```bash
zmake build npcx_monitor
```

The monitor file is located in the build output directory.

```bash
./build/zephyr/npcx_monitor/build-singleimage/npcx_monitor.bin
```

### Building with `ec-devutils`
The ebuild `zephyr-npcx-monitor` builds the NPCX monitor and installs into
the filesystem in the chroot or on the DUT.

To build the monitor only:

```bash
cros workon --host start zephyr-npcx-monitor
sudo emerge zephyr-npcx-monitor
```

`zephyr-npcx-monitor` is a dependency of `ec-devutils`, so you can also
build `ec-devutils` to install a new version in the chroot.

```bash
cros workon --host start zephyr-npcx-monitor
sudo emerge ec-devutils
```

The monitor binary is installed into your chroot with the other EC utilities:

```bash
/usr/share/ec-devutils/npcx_monitor.bin
```

## UART Update Workflow
Nuvoton EC's can be be reprogrammed using just a UART interface.  This workflow
is available even if the internal flash of the EC is erased or corrupted.

This is the workflow to program a new firmware image over the UART interface.
1. Assert the FLPRG1# or FLPRG2# to a physical low state.
    * Assert FLPRG1# to flash over the UART1 interface
    * Assert FLPRG2# to flash over the UART2 interface
1. Reset the EC
1. On reboot, the NPCX bootrom samples the flash program strap pins. If either
   is asserted the bootrom skips reading the image header from flash and waits
   for commands over the UART interface.
1. Call the `uartupdatetool` to load the `npcx_monitor.bin` file into RAM.
   Replace `<tty-dev>` with the UART device name found under the `/dev/`
   directory.
```bash
    uartupdatetool --port=<tty-dev> --baudrate=115200 --opr=wr --addr=0x200C3020 --file npcx_monitor.bin
```
1. Program flash.  Replace `<fw.bin>` with the name of the firmware image to
write - typically `build/zephyr/<board>/output/ec.bin`
```bash
    uartupdatetool --port=<tty-dev> --baudrate=115200 --auto --offset=0 --file <fw.bin>
```
1. De-assert the flash program pin.
1. Reset the EC
1. Your new image should boot and run

Note that the [`flash_ec`] script implements the UART update workflow as part
of the `npcx_uut` EC chip type.  The [`flash_ec`] script searches for
the NPCX monitor in several locations, but gives priority to the
`npcx_monitor.bin` found in the board's output build directory.


[servo dockerfile]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/hdctools/servo/dockerfiles/Dockerfile
[`flash_ec`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/flash_ec
[`check_hash.cmake`]: ./check_hash.cmake
