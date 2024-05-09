# Migration of FPMCU device tests to Zephyr

Authors: dawidn@google.com

Reviewers: tomhughes@chromium.org

Last Updated: 2024-05-09

[TOC]

## Objective

Run all tests that are listed in the
[run\_device\_tests.py](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/02bbda88c343f6b2697b00c8115b2f7e032563d1:src/platform/ec/test/run_device_tests.py;l=279)
with Zephyr FPMCU firmware.

## Background

There is a dedicated
[run\_device\_tests.py](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/test/run_device_tests.py)
script that runs tests on FPMCU. The tests are run on a dev board like
dragonclaw, with the servod connected. The script builds full firmware with a
test, flashes it, runs with a console command and checks results. For some
tests, the script also runs an additional console command e.g. _reboot ro_, or
runs _dut-control_, e.g. _dut-control fpmcu\_slp:1 _if it is needed.

As a part of migration FPMCU to Zephyr, the tests have to be migrated as well.

## Requirements

*   The migrated tests cover at least the same amount of functionality
*   Fully automated
*   Use native Zephyr solution as much as possible

## Design ideas

There are a few rather independent topics to cover.

### Test Framework

For tests written in C,
[Ztest](https://docs.zephyrproject.org/latest/develop/test/ztest.html) is an
obvious choice. There are plenty of such tests in EC and Zephyr.

For Cpp tests - try to use GTest, but this has to be explored and confirmed.
Otherwise use Ztest as well.

### Migrating test code

From migrating perspective there are 3 types of test:

*   Tests to skip
*   Tests to copy with some adjustments
*   Tests to copy almost 1:1

Some tests may not be viable with Zephyr for a few reasons, e.g. functionality
is not supported with Zephyr or a feature is already tested by Zephyr tests. To
skip a test, an additional flag in the run\_device\_tests script should be
[used with a reason](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5314362/6).

Some tests should use the same logic but need some adjustments.
[The abort test](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5307705)
is a good example. There is a check of the panic data after the abort call.
CrosEC uses
[its version](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/libc/syscalls.c;l=37)
of the abort function, which sets a panic reason. Zephyr uses
[its version](https://github.com/zephyrproject-rtos/zephyr/blob/main/lib/libc/common/source/stdlib/abort.c).
That means the check of the panic data is different - Zephyr uses saved
registers and CrosEC checks the panic reason. Additionally, Zephyr version uses
its mechanism (syswork\_q) to run a test after reboot caused by a test (test
staging).

There is a way to “translate” CrosEC tests into Zephyr Ztest with a mapping done
in
[test\_util.h](https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/unit_tests.md#file-headers).
However the mentioned adjustments would require many #ifdef statements in the
test code and additional shim layer for test staging, which is what we want to
avoid. It would probably cause more unexpected problems.

There are also tests that will be copy-paste with test asserts replacement. As
it is not a perfect solution, it is hard to find a better one. To avoid
desynchronisation of the CrosEC and Zephyr tests, there is a
[precheck](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5488762/2)
that displays a warning when a CrosEC version is edited.

Additionally, there are tests that can be run not only on hardware. These ones
should be placed alongside other tests and used with a proper config, e.g.
[crc test](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5353808/6).

### Building and running

CrosEC builds a full firmware image + test, which is run with a console command.
Zephyr follows that approach. There are already a few
[configs](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5307704)
that allow adding test code to a firmware image built with zmake.

The run\_device\_tests.py script has been already
[adjusted](https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/5310657)
to build Zephyr EC with zmake and flash it with flash\_ec. It is able to parse a
[twister style](https://docs.zephyrproject.org/latest/develop/test/twister.html#test-cases)
yaml file, to set proper configs per test. The yaml file is at
[hardcoded location](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/05ac8c0ac14d65457f27d16a210251dd87102545:src/platform/ec/test/run_device_tests.py;l=75)
at the moment.

## Alternatives considered

An obvious alternative is using twister, but it implicates a few changes.

Twister doesn’t use zmake -
[no](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/05ac8c0ac14d65457f27d16a210251dd87102545:src/platform/ec/zephyr/program/fpmcu/bloonchipper/bloonchipper.dts;l=48)
RO/RW parts and rollback regions. Some tests run by run\_device\_tests.py
require that.

Using twister also means setting configs per test. It is a correct approach for
unit tests e.g. CRC test doesn’t need fingerprint code. However there are more
complex examples which would require additional work to prepare a sufficient set
configs per test.

Additionally, there are extra actions taken by the run\_device\_tests.py script
that are not supported by twister by default. It includes changing WP, setting
fp\_sensor\_sel and fpmcu\_slp signals and measuring power consumption. Flashing
would also need to be investigated.

The twister alternative is definitely worth exploring. It looks like the main
task would be to group tests into types and clearly mark those ones that are not
pure unit tests and require additional, external actions. It would allow us to
switch to fully native Zephyr testing solutions.
