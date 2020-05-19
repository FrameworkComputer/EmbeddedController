# Code Coverage

Provides an overview of how to use code coverage tools when running the unit
tests in the EC codebase.

[TOC]

## Availability

Code coverage is only available for host-based unit tests, as opposed to
manual tests that run on target hardware.

## Building for code coverage

To build host-based unit tests for code coverage, invoke `make` with the
`coverage` target, as follows:

`make coverage -j`

This target will compile and link the unit tests with `--coverage` flag (which
pulls in the `gcov` libraries), run the tests, and then process the profiling
data into a code coverage report using the `lcov` and `genhtml` tools.

The coverage report top-level page is `build/host/coverage_rpt/index.html`.

### `make clobber` is required

**Always** `make clobber` when switching from building with code coverage
to building without code coverage, or from building without code coverage
to building with code coverage. `make clean` is not sufficient.

`make buildall -j ; make clobber ; make coverage -j`

`make coverage -j ; make clobber ; make buildall -j`

If you do not `make clobber`, you will get link-time errors such as:

```
core/host/task.c:558: undefined reference to `__gcov_init'
build/host/online_calibration/RO/core/host/timer.o:(.data+0x5b0): undefined reference to `__gcov_merge_add'
```

Note that `make clobber` will delete the coverage report.

### Noise in the build output

When building for code coverage, you may see multiple warnings of the form
`geninfo: WARNING: no data found for /mnt/host/source/src/platform/ec/core/host/cpu.h`
and
`genhtml: WARNING: function data mismatch at /mnt/host/source/src/platform/ec/common/math_util.c:134`

These warnings can be ignored. (FYI, the "function data mismatch" warnings
appear to be caused in part by using relative paths instead of absolute paths.)
