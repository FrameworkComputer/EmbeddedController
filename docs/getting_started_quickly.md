# Get Started Building EC Images (Quickly)

[TOC]

The
[Chromium OS Developer Guide](https://chromium.googlesource.com/chromiumos/docs/+/master/developer_guide.md)
and [README](../README.md) walk through the steps needed to fetch and build
Chromium OS source. These steps can be followed to retrieve and build EC source
as well. On the other hand, if your sole interest is building an EC image, the
general developer guide contains some extra unneeded steps.

The fastest possible way to build an EC image is to skip the Chromium OS chroot
install entirely. The following steps have been tested on an Ubuntu 15.10 (Wily
Werewolf) 64-bit host machine. Other distros / versions may be used, but
toolchain incompatibilities may require extra debug.

## Building

1.  Install build / dev tools:

    ```bash
    sudo apt-get install git libftdi-dev libusb-dev libncurses5-dev gcc-arm-none-eabi
    ```

1.  Sync the cros-ec git repo:

    ```bash
    git clone https://chromium.googlesource.com/chromiumos/platform/ec
    ```

1.  Build your EC image:

    ```bash
    HOSTCC=x86_64-linux-gnu-gcc make BOARD=$board
    ```

## External Dependencies

Most boards are buildable, but some will fail due to dependencies on external
binaries (such as [`futility`](#building-futility)). Also, some related tools
(such as `flash_ec` and `servod`) must be run from the Chromium OS chroot. Here
is a set of steps to setup a minimal development environment to build EC images
from the Chromium OS chroot:

1.  Create a folder for your chroot:

    ```bash
    mkdir cros-src; cd cros-src
    ```

1.  Run

    ```bash
    repo init -u https://chromium.googlesource.com/chromiumos/manifest.git --repo-url https://chromium.googlesource.com/external/repo.git -g minilayout
    ```

1.  Edit `.repo/manifest.xml`, and add `groups="minilayout"` to the platform/ec
    project, so the line becomes:

    ```
    <project path="src/platform/ec" name="chromiumos/platform/ec" groups="minilayout" />
    ```

1.  Run `repo sync`:

    ```bash
    repo sync -j <number of cores on your workstatsion>
    ```

1.  Enter the chroot and enter your password for `sudo` if prompted:

    ```bash
    ./chromite/bin/cros_sdk
    ```

1.  Set up your board:

    ```bash
    ./setup_board --board=$BOARD
    ```

    (ex. `./setup_board --board=glados`)

1.  Build EC:

    ```bash
    ./build_packages --board=$BOARD chromeos-ec
    ```

1.  Now, EC images for any board can be built with:

    ```bash
    cd ~/trunk/src/platform/ec; make BOARD=$board -j
    ```

## Building `futility` outside the chroot {#building-futility}

If you want to build the `futility` host tool outside the normal Chrome OS
chroot self-contained environment, you can try the following

1.  Install futility build dependencies:

    ```bash
    sudo apt-get install uuid-dev liblzma-dev libyaml-dev libssl-dev
    ```

1.  Get the vboot reference sources:

    ```bash
    git clone https://chromium.googlesource.com/chromiumos/platform/vboot_reference
    ```

1.  Build it:

    ```bash
    cd vboot_reference ; make
    ```

1.  Install it in `/usr/local/bin`:

    ```bash
    sudo make install
    ```

1.  Add `/usr/local/bin` to your default `PATH`:

    ```bash
    export PATH="${PATH}:/usr/local/bin"
    ```
