# Zephyr EC

[TOC]

## Introduction

Zephyr EC is an effort to create an industry-standard Embedded Controller
implementation for use primarily on laptops. It is born out of the Chromium OS
EC.

## native-posix development

Zephyr can be built to run on your host machine, making it easier to develop
and test features. This is called the minimal-posix build.

To build it::

```
  cd ~/chromium/src/platform/ec
  zmake build minimal-posix
```

and run it:

```
  build/zephyr/minimal-posix/build-singleimage/zephyr/zephyr.exe
```

Check the display for the pseudotty and connect an xterm to it, e.g.:

```
   xterm -e screen /dev/pts/28
```

You will then see the EC prompt and you can type commands, e.g. type 'help':

```
  Please press the <Tab> button to see all available commands.
  You can also use the <Tab> button to prompt or auto-complete all commands or its subcommands.
  You can try to call commands with <-h> or <--help> parameter for more information.

  Available commands:
    cbi        :Print or change Cros Board Info from flash
    chan       :Save, restore, get or set console channel mask
    crash      :Crash the system (for testing)
    feat       :Print feature flags
    gettime    :Print current time
    gpioget    :Read GPIO value(s)
    gpioset    :Set a GPIO
    help       :Prints the help message.
    hibernate  :Hibernate the EC
    kernel     :Kernel commands
    log        :Commands for controlling logger
    md         :dump memory values, optionally specifying the format
    panicinfo  :Print info from a previous panic
    reboot     :Reboot the EC
    rw         :Read or write a word in memory optionally specifying the size
    shmem      :Print shared memory stats
    sysinfo    :Print system info
    syslock    :Lock the system, even if WP is disabled
    timerinfo  :Print timer info
    version    :Print versions
    waitms     :Busy-wait for msec (large delays will reset)
```

Use Ctrl-C to quit (from the main terminal) as normal.

You can run zephyr under gdb just like any other program.

If you want to share the same terminal, add this line to
`zephyr/program/minimal/prj.conf` and rebuild:

```
  CONFIG_NATIVE_UART_0_ON_STDINOUT=y
```

Running that will show an EC prompt on your terminal. Use Ctrl-\ to quit.

## CQ builder

To test the cq builder script run these commands:

### firmware-zephyr-cq

```
rm -rf /tmp/artifact_bundles /tmp/artifact_bundle_metadata \
 ~/chromiumos/src/platform/ec/build
( cd ~/chromiumos/src/platform/ec/zephyr ; \
./firmware_builder.py --metrics /tmp/metrics-build build && \
./firmware_builder.py --metrics /tmp/metrics-test test && \
./firmware_builder.py --metrics /tmp/metrics-bundle bundle && \
echo PASSED )
cat /tmp/artifact_bundle_metadata
cat /tmp/metrics-build
ls -l /tmp/artifact_bundles/
```

### firmware-zephyr-cov-cq

```
rm -rf /tmp/artifact_bundles-cov /tmp/artifact_bundle_metadata-cov \
  ~/chromiumos/src/platform/ec/build && \
cd ~/chromiumos/src/platform/ec/zephyr && \
./firmware_builder.py --metrics /tmp/metrics --code-coverage build && \
./firmware_builder.py --metrics /tmp/metrics --code-coverage test && \
./firmware_builder.py --metrics /tmp/metrics --code-coverage \
  --output-dir=/tmp/artifact_bundles-cov \
  --metadata=/tmp/artifact_bundle_metadata-cov bundle && \
echo PASSED
cat /tmp/artifact_bundle_metadata-cov
ls -l /tmp/artifact_bundles-cov
```

## Zmake unit tests

Run the tests with `zephyr/zmake/run_tests.sh`.  You can generate a coverage
report, but not in the chroot, as some pip modules are missing there.

You can run the coverage report outside of the chroot easily:

```
# Install test dependencies
cd ~/chromiumos/src/platform/ec
python3 -m pip install 'zephyr/zmake[tests]' --user
# Run tests with coverage
cd ~/chromiumos/src/platform/ec/zephyr/zmake
coverage run --source=zmake -m pytest .
coverage report
coverage html
xdg-open htmlcov/index.html
```
