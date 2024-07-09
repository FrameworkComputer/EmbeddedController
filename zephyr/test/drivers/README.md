This is the drivers test directory. The goal is to have many driver test suites
in few binaries, so that compile time will be faster than many small tests, and
so we can test interactions between different subsystems easily.

## Run all the test suites

```bash
(chroot) ec $ ./twister -T zephyr/test/drivers
```

To see all the output of twister in stdout (for example if the build fails)

```bash
(chroot) ec $ ./twister -v -i -T zephyr/test/drivers
```

## Code coverage

See the [EC code coverage] doc.

## Debugging

You need the host version of gdb:

```bash
(chroot) sudo emerge -j sys-devel/gdb
```

Build all the drivers tests
```bash
(chroot) ec $ ./twister -b -T zephyr/test/drivers
```

Then run gdb

Example of running gdb on the `drivers.default` test binary:

```
(chroot) ec $ gdb twister-out/native_sim/drivers.default/zephyr/zephyr.exe
# Set breakpoints, run, etc.
```

Another of running gdb now on the `drivers.chargesplash` test binary:

```
(chroot) ec $ gdb twister-out/native_sim/drivers.chargesplash/zephyr/zephyr.exe
```

[EC code coverage]: ../../../docs/code_coverage.md#zephyr-ztest-code-coverage
