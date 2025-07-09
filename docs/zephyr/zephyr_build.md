# Building Zephyr OS

[TOC]

Chromium OS EC uses the `zmake` tool to build Zephyr.

This section describes how to build and use zmake.

## Environment Setup

Follow the [Chromium OS Developer Guide] to sync the source and get
the chroot setup.

It is also possible to build without a Chromium OS chroot, albeit with
additional setup steps.  See [out-of-chroot building] if this
interests you.

### A Note on Syncing the Source

The Legacy EC required only a single repository, and thus, it was common
for developers to sync just the EC repository using `repo sync .`.

Since the Zephyr build relies on multiple repositories, doing so will
likely end you up in a broken state.  Please be sure to do a complete
sync when updating the source.

## Building

To build the EC for a single project, run:

``` shellsession
(chroot) $ zmake build "${PROJECT}"
```

For example, to build the EC for `skyrim`, run:

``` shellsession
(chroot) $ zmake build skyrim
```

The output binary will then be located at `build/zephyr/skyrim/output/ec.bin`.

Additional output files you may find useful:

*   `build/zephyr/skyrim/output/zephyr.ro.elf` - read-only ELF for debugging
*   `build/zephyr/skyrim/output/zephyr.rw.elf` - read-write ELF for debugging

You might also find these files useful (using read-only as an example):

*   `build/zephyr/skyrim/build-ro/zephyr/.config` - Kconfig options selected
*   `build/zephyr/skyrim/build-ro/zephyr/include/generated/devicetree_unfixed.h` - the (large)
    header file that zephyr uses to provide devicetree information to the C code
*   `build/zephyr/skyrim/build-ro/zephyr/zephyr.dts` - devicetree that is used
*   `build/zephyr/skyrim/build-ro/zephyr/zephyr.dts` - map of image

For a complete list of `zmake` commands and options, see the
[Zmake Documentation].

### Building from Portage

There is also a [an ebuild] for integration into the larger Chromium
OS build system.

To build using this flow, run:

``` shellsession
(chroot) $ emerge-${BOARD} chromeos-base/chromeos-zephyr
```

### Looking at the Kconfig

It should be possible to do this with:

```bash
ninja -C /tmp/z/volteer/build-ro menuconfig
```

However at present this does not work [b/184662866](http://b/184662866).


[Chromium OS Developer Guide]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md
[Zmake Documentation]: ../../zephyr/zmake/README.md
[an ebuild]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/HEAD/chromeos-base/chromeos-zephyr/chromeos-zephyr-9999.ebuild
[out-of-chroot building]: ./out_of_chroot.md
