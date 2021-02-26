# Zephyr OS-based EC Initialization Order

Zephyr provides Z_INIT_ENTRY_DEFINE() & the extend macro to install the initial
function. The initialize flow for different levels would be like the following
(not very detailed):
*   architecture-specific initialization
*   `PRE_KERNEL_1` level
*   `PRE_KERNEL_2` level
*   `POST_KERNEL` level
*   `APPLICATION` level
*   main()

The kernel and driver initial functions separate into specific initialize
levels. It couldn't put all initial functions in main() for the Zephyr OS-based
EC. It is also hard to maintain those initial priority which separates into
different files.

This file defines some Zephyr OS-based EC initial priorities which have critical
sequence requirement for initializing:

## PRE_KERNEL_1
*   Priority (0-9) - Reserved for system testability:

    The highest priority could be used in zephyr. Don't use it when system
    development. Buffer it for the following system development & testing.

*   Priority (10-19) - Chip level system pre-initialization:

    These priorities in this range are used for critical chip initialization,
    including determining the reset cause and initializing the battery-backed
    RAM driver. Most chip drivers should only be initialized after
    `PLATFORM_EC_SYSTEM_PRE_INIT`.

*   Priority (20) - PLATFORM_EC_SYSTEM_PRE_INIT:

    At this initialization priority, the CROS system checks the reset cause and
    initializing the system reset flags. Any chip level drivers related to
    determining the reset type must be at a higher priority.

*   TODO

## PRE_KERNEL_2
*   TODO

## POST_KERNEL
*   TODO

## APPLICATION
*   TODO

## main()
*   TODO
*   Start the tasks.
