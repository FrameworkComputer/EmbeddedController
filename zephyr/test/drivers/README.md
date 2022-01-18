This is the combined driver test. The goal is to have many driver test suites
in one binary, so that compile time will be faster than many small tests, and
so we can test interactions between different subsystems easily.

## Run all the test suites

```bash
(chroot) zmake configure --test zephyr/test/drivers
```

To see all the output of zmake (for example if the build fails)

```bash
(chroot) zmake -l DEBUG -j 1 configure --test zephyr/test/drivers
```

## Code coverage

To calculate code coverage for this test only

```bash
(chroot) zmake configure --coverage --test zephyr/test/drivers
(chroot) genhtml --branch-coverage -q \
        -o build/zephyr/test-drivers/output/coverage_rpt \
        build/zephyr/test-drivers/output/zephyr.info
```

The report will be in build/zephyr/test-drivers/output/coverage_rpt/index.html

## Debugging

You need the host version of gdb:

```bash
(chroot) sudo emerge -j sys-devel/gdb
```

Build the test
```bash
(chroot) zmake configure --build zephyr/test/drivers
```

Then run gdb

```
(chroot) gdb build/zephyr/test-drivers/build-singleimage/zephyr/zephyr.exe
# Set breakpoints, run, etc.
```

