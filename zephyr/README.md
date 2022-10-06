# Zephyr EC

[TOC]

## Introduction

Zephyr EC is an effort to create an industry-standard Embedded Controller
implementation for use primarily on laptops. It is born out of the Chromium OS
EC.

## Gitlab integration

As an experiment we have a basic gitlab integration. It watches the EC repo and
kicks of a build when new commits appear. So far it just builds for volteer and
does not run any tests. For firmware branches, it also builds, but fails.

The gitlab builder works without a chroot and uses the Zephyr toolchain. This
is intended to ensure that we have a path to upstreaming our code eventually and
do not rely on Chrome OS-specific tools. It does make use of 'zmake', however.

See the piplines [here](https://gitlab.com/zephyr-ec/ec/-/pipelines).

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

The [latest coverage report](https://gitlab.com/zephyr-ec/ec/-/jobs/artifacts/main/file/zephyr/zmake/htmlcov/index.html?job=zmake_coverage
) is on gitlab.

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
