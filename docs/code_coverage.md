# Code Coverage

Provides an overview of how to use code coverage tools when running the unit
tests in the EC codebase.

[TOC]

## Availability

Code coverage is only available for host-based unit tests, as opposed to manual
tests that run on target hardware.

## Building for code coverage

To build host-based unit tests for code coverage, invoke `make` with the
`coverage` target, as follows:

`make coverage -j`

This target will compile and link the unit tests with `--coverage` flag (which
pulls in the `gcov` libraries), run the tests, and then process the profiling
data into a code coverage report using the `lcov` and `genhtml` tools.

The coverage report top-level page is `build/coverage/coverage_rpt/index.html`.

### Noise in the build output

When building for code coverage, you may see multiple warnings of the form
`geninfo: WARNING: no data found for
/mnt/host/source/src/platform/ec/core/host/cpu.h` and `genhtml: WARNING:
function data mismatch at
/mnt/host/source/src/platform/ec/common/math_util.c:134`

These warnings can be ignored. (FYI, the "function data mismatch" warnings
appear to be caused in part by using relative paths instead of absolute paths.)

## Zephyr ztest code coverage

To build the Zephyr unit tests for code coverage run:

`zmake coverage build/ztest-coverage`

This target will compile, without linking, all zephyr projects with
`CONFIG_COVERAGE` Kconfig option enabled, run the tests, and then process the
profiling data into a code coverage report using the `lcov` and `genhtml`
tools. This requires the `HAS_COVERAGE_SUPPORT` option, which can only be
selected in `Kconfig.board`.

The coverage report top-level page is
`build/ztest-coverage/coverage_rpt/index.html`.

For manual coverage report you can run:
`zmake configure --test --coverage <PATH>`

Example:
`zmake configure --test --coverage zephyr/test/drivers/`
`genhtml -q -o build/ztest-coverage/coverage_rpt/ build/zephyr/test-drivers/output/zephyr.info`
