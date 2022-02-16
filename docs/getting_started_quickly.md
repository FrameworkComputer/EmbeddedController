# Get Started Building EC Images (Quickly)

[TOC]

The
[Chromium OS Developer Guide](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md)
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
    sudo apt-get install git libftdi-dev libusb-dev libusb-1.0-0-dev \
         libncurses5-dev gcc-arm-none-eabi
    ```

1.  Clone the cros-ec git repo:

    ```bash
    git clone https://chromium.googlesource.com/chromiumos/platform/ec
    ```

1.  Select a target board

    ```bash
    export BOARD=<target board>
    ```

    Refer to the [Chromium OS Developer
    Guide](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md)
    for more details about selecting the appropriate target board. If you are
    just getting your environment set up and are unsure which board you will be
    targeting then you may select a board from `board/` or just
    choose a safe default, such as `nocturne`.

1.  Build your EC image:

    ```bash
    HOSTCC=x86_64-linux-gnu-gcc CROSS_COMPILE_arm=arm-none-eabi- make BOARD=${BOARD}
    ```

    Note: the EC supports multiple architectures, check `core/*/build.mk` files
    for other supported `CROSS_COMPILE_` variables.

## External Dependencies

Most boards are buildable, but some will fail due to dependencies on external
binaries (such as [`futility`](#building-futility)). Also, some related tools
(such as `flash_ec` and `servod`) must be run from the Chromium OS chroot. Here
is a set of steps to setup a development environment to build EC images from the
Chromium OS chroot:

1.  Create a folder for your chroot:

    ```bash
    mkdir chromiumos; cd chromiumos
    ```

1.  Initialize the checkout in the current directory:

    ```bash
    repo init -u https://chromium.googlesource.com/chromiumos/manifest -b stable
    ```

    NOTE: The
    [`-b stable` flag](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Sync-to-Green)
    only works for Googlers. Remove it if you are developing externally.

1.  Update the working tree to the latest version:

    ```bash
    repo sync -j <number of cores on your workstatsion>
    ```

1.  Enter the chroot (type your password for `sudo` if prompted):

    ```bash
    cros_sdk
    ```

1.  Select a target board

    ```bash
    export BOARD=<target board>
    ```

    See previous section for recommendations.

1.  Set up your board:

    ```bash
    setup_board --board=${BOARD}
    ```

1.  Build EC:

    ```bash
    build_packages --board=${BOARD} chromeos-ec
    ```

1.  Now, EC images for any board can be built with:

    ```bash
    cd ~/trunk/src/platform/ec; make BOARD=${BOARD} -j
    ```

## Building `futility` outside the chroot {#building-futility}

If you want to build the `futility` host tool outside the normal Chrome OS
chroot self-contained environment, you can try the following:

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
