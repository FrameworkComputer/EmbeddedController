# Get Started Building EC Images (Quickly)

[TOC]

The
[Chromium OS Developer Guide](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md)
and [README](../README.md) walk through the steps needed to fetch and build
Chromium OS source. These steps can be followed to retrieve and build EC source
as well. On the other hand, if your sole interest is building an EC image, the
general developer guide contains some extra unneeded steps.

## Building

Here is a set of steps to set up a development environment to build EC images
inside the Chromium OS chroot:


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