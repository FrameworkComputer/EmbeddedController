# ChromeOS EC Firmware Code Test Requirements

[TOC]

## Overview

The ChromeOS EC firmware requires that all C source and header file code changes
include tests covering at least 90% of any new or changed lines of code. A
non-blocking warning comment is posted on CLs that do not exceed 95% coverage
This change is in effect as of 2023-01-19.

The Gerrit commit-queue enforces the coverage requirement with the
`firmware-zephyr-cov-cq` builder.

## Rationale

Starting 2H’22, all new Chromebook projects use the Zephyr RTOS as the
foundation for the ChromeOS EC firmware.  As part of the switch to Zephyr RTOS,
Google wrote unit and integration tests to verify functionality of the EC
firmware.  These tests run directly on your development system using the
native_sim environment and do not require any Chromebook hardware.

These tests improve the quality of the EC firmware, reducing the number of EC
crashes and bugs found on shipping Chromebooks.

The ChromeOS EC firmware requires that all projects based on the Zephyr RTOS
achieve code coverage of 90% or better.

## Resources for writing tests

* The [Zephyr Testing] documentation in the ChromeOS EC firmware provides the
  most up-to-date information for writing tests specifically for the ChromeOS EC
  firmware.

* [Zephyr Test Framework] documentation in the Zephyr Project provides general
  information about the test framework used by Zephyr RTOS.

### Testing project specific code

* The board

### Generating coverage reports

* [Zephyr ztest code coverage] provides instructions for generating code coverage
reports locally on your machine.

* [Code Coverage in CQ] provides details for viewing the coverage information
  directly from Gerrit or by using Google's code search tool.

### Bypassing the code coverage requirement

In limited cases, you may amend your commit message to include the
`LOW_COVERAGE_REASON` tag. This tag bypasses the code coverage requirement
enforced by Gerrit. Simply add the tag followed by a description to justify
bypassing code coverage. You must include a reference to a bug (in the form
`b:1234567` or `b/1234567`) that tracks whatever issue is impeding coverage.

```
LOW_COVERAGE_REASON=no emulator for the ANX7483 exists b/248086547
```

Permissible reasons for bypassing the code coverage requirements include:

* Fixing a high-priority bug that blocks a release or some other milestone.

* Fixing a bug in an existing driver that is currently untested.

* Modifying on-chip EC peripheral drivers. See the [Known Issues](#known_issues)
  section below for details.

When bypassing code coverage, please open a bug to track the work required to
create tests.

Reviewers may reject your low coverage reason and request that you update or add
tests for your change.

## Exceptions to code coverage

### Legacy EC code

Zephyr EC projects share large portions of the legacy code found under the
[`platform/ec`] repository. The code coverage requirements apply to the shared
code, with exceptions for the following directories:

* [`platform/ec/baseboard`]
* [`platform/ec/board`]
* [`platform/ec/chip`]

### Known issues

There are no tests for the chip specific code found under the
[`platform/ec/zephyr/drivers`] directory. Due to include file conflicts, it is
not currently known how to test these drivers.

### False positives

The coverage tool may falsely mark a line of code as executable but the line of
code is not reachable. An example could be a switch statement, but the default
label is not reachable. In these cases add a comment using exclusion markers,
`LCOV_EXCL_*`,  to the file.  See the [geninfo manpage] for more information.

Do not use exclusion markers to bypass executable code or to avoid writing tests
for difficult to reach code.


[`platform/ec/baseboard`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard
[`platform/ec/board`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board
[`platform/ec/chip`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/chip
[`platform/ec/zephyr/drivers`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/drivers
[Zephyr Testing]: ./zephyr/ztest.md
[Zephyr Test Framework]: https://docs.zephyrproject.org/latest/develop/test/ztest.html
[Zephyr ztest code coverage]: ./code_coverage.md#Zephyr-ztest-code-coverage
[Code Coverage in CQ]: ./code_coverage.md#code-coverage-in-cq
[`platform/ec`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/
[geninfo manpage]: https://manpages.debian.org/unstable/lcov/geninfo.1.en.html
