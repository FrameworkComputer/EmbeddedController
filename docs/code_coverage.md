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

This needs some work, but you can generate coverage reports with these commands:

```
# Get initial (0 lines executed) coverage for as many boards as possible
for project in $(cd zephyr/projects; find -name zmake.yaml -print)
do
        project="$(dirname ${project#./})"
        echo "Building initial coverage for ${project}"
        builddir="build/ztest-coverage/projects/$project"
        infopath="build/ztest-coverage/projects/${project/\//_}.info"
        zmake configure --coverage -B ${builddir} zephyr/projects/$project
        for buildsubdir in ${builddir}/build-* ; do ninja -C ${buildsubdir} all.libraries ; done
        lcov --gcov-tool $HOME/trunk/src/platform/ec/util/llvm-gcov.sh -q -o - -c -d ${builddir} -t "${project/\//_}" \
          --exclude "*/build-*/zephyr/*/generated/*" --exclude '*/test/*' --exclude '*/testsuite/*' \
          -i | util/normalize_symlinks.py >${infopath}
done

# Get unit test coverage
for i in zephyr/test/* ; do
        builddir="build/ztest-coverage/$(basename $i)"
        zmake configure --coverage --test -B ${builddir} $i
        lcov --gcov-tool $HOME/trunk/src/platform/ec/util/llvm-gcov.sh -q -o - -c -d ${builddir} -t "$(basename $i)" \
          --exclude '*/build-singleimage/zephyr/*/generated/*' --exclude '*/test/*' --exclude '*/testsuite/*' \
          | util/normalize_symlinks.py >${builddir}.info
done

# Merge into a nice html report
genhtml -q -o build/ztest-coverage/coverage_rpt -t "Zephyr EC Unittest" -p /mnt/host/source/src -s build/ztest-coverage/*.info build/ztest-coverage/projects/*.info
```
