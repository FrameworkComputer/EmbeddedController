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

## News and Breaking Changes

* Support for Zephyr v2.4 has been dropped as of
  [CL:2715345](https://crrev.com/c/2715345).  If you wish to build for
  kernel v2.4 now, you must invoke `zmake configure` similar to below:

      $ zmake --zephyr-base ~/trunk/src/third_party/zephyr/main/v2.4 \
              configure --ignore-unsupported-zephyr-version ...
