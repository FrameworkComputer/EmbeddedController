# EC Add-in-card (AIC) Tests

The tests in this directory are compiled and run on the EC AIC connected to the
Dagwood tester.

Test output is generated from the EC UART, sent through the UART to USB bridge
in the Dagwood firmware, and consumed by twister running on the host system.

## Building and running tests

Zephyr's test runner [Twister] is used to identify the tests that can be flashed
and run on the EC AICs.

This is currently only supported in the chroot environment, and you must also
have the dagwood repository available.

All the commands must be executed from the chroot environment and the current
directory must be `~/chromiumos/src/platform/ec`

### Dagwood hardware setup

1. Connect an EC AIC (ITE, Nuvoton, or Realtek) to your Dagwood board.
1. Connect a Dagwood board to your host system using a USB Type-C cable.
1. Update the Dagwood firmware by following the instructions in the [Dagwood
README].
1. After flashing the Dagwood firmware, reboot the Dagwood board by pressing
the `NRST` reset button.
1. Verify the Dagwood TTY ports are visible.  This can be run inside or outside
the chroot.
    ```bash
    $ $ find /dev -name "ttyACM*"
    /dev/ttyACM2
    /dev/ttyACM1
    /dev/ttyACM0
    ```

### Building the tests only
Use Twister's  `-b` option to build the tests only.

```bash
./twister -ivc -T zephyr/test/ec-aic --toolchain=coreboot-sdk \
  -p realtek/rts5912 \
  -b
```

### Building and running all the Dagwood tests

Use the `-T zephyr/test/ec-aic` option to run all tests targeting EC AIC boards.

Flashing the EC and running the tests requires the `--device-testing` and the
`--device-serial /dev/ttyACM1` options.

This assumes you only have one Dagwood board connected to your host machine.

```bash
./twister -ivc -T zephyr/test/ec-aic --toolchain=coreboot-sdk \
  -p realtek/rts5912 \
  --device-testing --device-serial /dev/ttyACM1 \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

### Running a single test
You can run just a single test by replacing the `-T` option with
`-s <test_name>`. Valid test names are found in the `testcase.yaml` files
found under the `zephyr/test/ec-aic` directory.

```bash
./twister -ivc -s aic.i2c --toolchain=coreboot-sdk \
  -p realtek/rts5912 \
  --device-testing --device-serial /dev/ttyACM1 \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

## Running tests on multiple Dagwood boards

To run tests on multiple Dagwood boards connected to the host, use the
[`./dagwood-hwmap`] file. You need to edit this file to specify the
serial ID and serial device path for each Dagwood connected.

A file header comment in [`./dagwood-hwmap`] provides details on how to modify
the hardware map.

To build and run the tests against all boards, replace the `--device-serial`
parameter with the `--hardware-map` option.

```bash
./twister -ivc -s aic.i2c --toolchain=coreboot-sdk \
  -p realtek/rts5912 -p npcx9/npcx9m7f \
  --device-testing --hardware-map zephyr/test/ec-aic/dagwood-hwmap \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

[Twister]: https://docs.zephyrproject.org/latest/develop/test/twister.html
[Dagwood README]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/dagwood/README.md
[`./dagwood-hwmap`]: ./dagwood-hwmap
