# Detachable Base Verified Boot

Authors: rspangler@google.com, drinkcat@google.com

Last Updated: 2016-11-16

Original: http://go/detachable-base-vboot

[TOC]

## Introduction

### What's a Base?

Detachable Chromebooks such as `Poppy` have a tablet-like `Lid` and a detachable
keyboard `Base`. Effectively, the `Base` is a USB keyboard+trackpad which plugs
into the `Lid`.

The `Lid` contains most of the components, including:

*   AP
*   ECDisplay
*   Storage
*   Battery

The `Base` connects to the `Lid` via USB pogo pins, and contains:

*   EC ([STM32F072]). To minimize confusion with the main EC in the `Lid`, this
    will always be called the `BaseEC`.
*   Matrixed keyboard
*   Touchpad

The `Base` always gets its power from the `Lid` USB port. This means that
attaching the base always triggers a power-on reset.

### Verified Boot Requirements

The `BaseEC` will be responsible for handling user input from the keyboard and
touchpad. This means that a compromised `BaseEC` could implement a keylogger. To
prevent this, we will use verified boot to protect the `BaseEC` firmware.

We need a way to securely update the `BaseEC` firmware from the AP. We cannot
use EC Software Sync as implemented on existing Chromebooks (and as still used
in the `Lid`) because the `Base` cannot trust that it is talking to an official
`Lid` firmware/OS. All the Base knows is that _something_ on the other end of
USB is trying to send it an update. So the BaseEC will need to do its own public
key verification of the firmware update. This includes rollback protection.

Updating the `BaseEC` firmware should not require rebooting the lid. This means
the update will take place after the OS has already booted on the lid. Ideally,
it should also not require the user to detach/reattach the base during the
update process. If the update takes longer than a few seconds, we should tell
the user, because the keyboard and trackpad will be unavailable during the
update.

The solution should also have low (or no) BOM cost, and minimal flash size
requirement.

## Proposal

`BaseEC` RO region includes a public key, whose private counterpart is kept
safely on our signers. On boot, RO checks RW signature (RW image is signed by
our signers), and will only jump to RW if the signature is valid.

We also include a rollback region (RB) to implement rollback protection (and
prevent rollback to a correctly signed, but compromised, RW). This region can
only be updated by RO.

We also devise a scheme to update RW firmware (the details are documented in
[EC Update over USB]).

Note: This proposal is very specific to the STM32 flash architecture. Other ECs
(particularly ones with external SPI flash) may need additional external logic
and/or a I2C EEPROM to hold the rollback info block.

### Flash

STM32F072 has 128KB flash, with 2KB erase sectors and 4KB protection blocks.

We will divide flash into three sections:

*   `BaseEC`-RO firmware.
    *   Not updatable in production.
    *   Only capable of USB update, not keyboard/trackpad.
    *   Contains public key to verify RW image (RSA-3072).
*   `BaseEC`-RW firmware.
    *   Fully functional.
    *   Updatable from AP.
    *   Signature (SHA-256 + RSA-3072).
*   `BaseEC`-RB: Rollback info block (4KB).
    *   Contains minimum RW version that RO will accept to jump to.
    *   Updatable from RO.

Each of those sections can be locked independently: In production, RO is always
locked, and only RO can write to RB (RO will always make sure to lock RB before
jumping to RW).

Flash protection is a little entertaining on STM32:

*   The flash protection bits for the \*next\* boot are stored in a non-volatile
    `WRPx` register (in EC code, this is abstracted as
    `EC_FLASH_PROTECT_[REGION]_AT_BOOT` flags)..
*   On chip reset, `WRPx` is copied into a read-only `FLASH_WRPR` register; that
    controls which blocks are protected for this boot. This is abstracted as
    `EC_FLASH_PROTECT_[REGION]_NOW` in the EC code.

### Rollback Info Block

The Rollback Info Block (aka "RB") is a 4KB block of flash.

It has two 2KB erase sectors. We will ping-pong writes to those sectors, so that
interrupting power during an erase-write cannot cause data loss. If both sectors
are valid, the stricter (i.e. the highest value) of the 2 sectors is used.

We will use the RB to hold the following:

*   Minimum **RW rollback firmware** version: a 32-bit integer. Used for
    rollback protection. This number is independent of the actual EC version,
    and is stored a 32-bit integer as part of the `BaseEC`-RW region (see
    [CL:452815] for a possible implementation)
*   A magic signature that indicates that the RB section is valid.

### RO Verified Boot Flow

#### Write-Protect RO think test before this handles corrupt RW.

Write protect of RO firmware works the same way it does now:

*   Early RO code looks at a write protect (WP) GPIO and a global PSTATE
    variable (part of the RO image itself). When we switch to RO that contains
    the MP key, we set the PSTATE to locked.
*   If both of those are set:
    *   RO code sets `EC_FLASH_PROTECT_RO_AT_BOOT` to protect itself. This
        ensures RO code is never writable past this point.
    *   If `_AT_BOOT` flags protects more than the current write protect range
        (`_NOW` flags), RO reboots so that changes take effect.
*   Otherwise, someone has physically disconnected WP. Set `WRPx=0` to unprotect
    all flash and reboot.

#### Check if AP Wants To Update RW

Next, RO needs to find out if the AP wants to update RW. RO initializes USB and
starts a 1 second timer to give the AP an opportunity to send a command before
RO jumps to RW. This delay gives us a way to regain control of the base, if the
previous RW firmware is properly signed but bad/nonfunctional.

That command can be:

*   `STOP_IN_RO`: Yes, I might want to update you. Stick around.
    *   `UNLOCK_RW`: Tells EC to unlock RW region, if it is currently locked, so
        that it can be reprogrammed. This also locks RB region. EC reboots if
        needed.
*   `JUMP_TO_RW`: No, I don't want to update you. Go ahead and jump to your RW
    code if it verifies.

RO will start verifying RW while it waits for the AP to send it a command or for
the timeout. If a command is received, RO will stop the 1-second timer, and wait
for more commands from the AP. This allows the AP to update RW.

Verifying RW will take ~200 ms, and the AP should be able to send a command to
the base within ~100 ms of it appearing on USB, so this check should not cause
any delay to the base's boot process.

#### Verify RW

RO calculates the hash of RW.

*   Use the public key stored in RO to check if the hash matches the RSA-signed
    RW signature. On failure, go back to waiting for an update from the AP.
*   Check the RW rollback version against the stored minimum version in RB. If
    the RW version is too low, fail. Go back to waiting for an update from the
    AP.
*   If RO is protected, then also set `EC_FLASH_PROTECT_RW_AT_BOOT` so that RW
    will be protected on the next boot, the reboot.

#### Roll Forward

If `EC_FLASH_PROTECT_ROLLBACK_NOW` is set (RB is protected), do not attempt to
roll forward. We know RW firmware is properly signed, but not if it's
functional.

If `EC_FLASH_PROTECT_ROLLBACK_NOW` is not set (RB is unprotected), \_and\_ the
RW signature is correct, then update RB:

*   Erase/write the older sector of RB.
*   Set the stored minimum version to the RW rollback version.
*   If RO is protected, then also set `EC_FLASH_PROTECT_ROLLBACK_NOW` so that RB
    will be protected on the next boot.

#### Jump to RW

If the 1-second timer for the AP to send a command to RO has not expired, RO
waits for it to expire or the AP to send a command, whichever happens first.

If RB or RW is unprotected (`EC_FLASH_PROTECT_RW/ROLLBACK_NOW` are not set),
protect it and reboot (we never want RW to be able to update RB on its own).

Otherwise, jump to RW firmware.

### RW Verified Boot Flow

RW firmware provides the keyboard and trackpad functionality.

#### AP Wants To Update RW

At some point the AP may want to update RW. To do so, it sends `UNLOCK_RW`
command, to ask RW to unlock itself and reboot, then follow the update steps
above.

#### AP Wants to Roll Forward RW

After the update, the base boots to the new RW firmware. At that point, the AP
knows the new RW firmware is good enough to talk to, so it tells RW to prepare
for roll forward.

*   `UNLOCK_ROLLBACK` command: RW unprotects RB.
*   On next boot (not necessarily urgent, but can be forced), RO will update RB
    according to the steps above.

### Write Protect GPIO

The `BaseEC` needs a write protect (WP) GPIO signal to decide whether to keep RO
firmware protected or not. This is the same requirement as on existing ECs.

In an assembled base, the WP signal will be physically asserted. De-asserting
the signal requires disassembling the base and disconnecting something.

Typically, the `BaseEC` will apply a weak pull-up to the WP GPIO; the presence
of the WP screw/flex will short the pin to ground.

#### RO Updates During Development

If RO is unprotected (i.e. during development), RW can also update it.

If the key is \_not\_ the same (dev->premp, premp->mp updates) we can't update
RW first (it won't verify). These steps should work though, if current RW is
recent enough and stable enough to update RO:

*   Make sure RW is active
*   Update RO, reboot
*   Update RW from RO

If the key is the same, we can update RW first.

### Signer, image format, and verification process

Memory map:

RO                                                | RB  | RW
------------------------------------------------- | --- | ---
`...` \| `Public key` \| `...` \| `FMAP` \| `...` |     | `EC code and data` \| `Blank (0xff)` \| `Signature`

*   RO contains an embedded RSA public key (`vb21_packed_key` format), at a
    variable location.
*   RW contains a signature (`vb21_signature`), packed at the end of the RW
    region.
    *   The signature also contains the actual length of the EC code and image
        (ignoring 0xff padding)
    *   RO validates signature against the provided length, then checks that the
        rest of the RW region (up to the signature itself) is filled with ones
        (padding).
        *   This speeds up verification significantly, as SHA-256 is an
            expensive process.
*   RO contains an FMAP that allows futility to find the RO key, RW region, and
    RW signature location.

For re-signing, `futility` (rwsig type) does this:

*   Look for FMAP to find RO public key RW region, and RW signature locations.
*   Resign RW region, using the length provided in existing RW signature.
*   Replace RO public key with the one used for signing.

`vb21_packed_key` (public key) has a field for key version, that we can use to
increment from dev keys, to premp, and final mp keys. BaseEC will need to report
the key version, to avoid incorrect updates.

## Example Boot / Update Flows

The base starts in the following state:

*   Powered off
*   WP GPIO is asserted
*   PSTATE is set to protect RO firmware
*   RW firmware is valid, and currently version M
*   `EC_FLASH_PROTECT_[REGION]_AT_BOOT/_NOW` protects RO+RW+RB (that is,
    everything)

All AP operations are done from the `Lid` OS.

Base updates will interrupt keyboard/trackpad functionality, so the user should
be informed when an update is taking place.

Reboots of the `Base` do not cause or require reboots of the `Lid`, do not
require action on the part of the user, and will not be visible to the user
(other than the previously noted lack of functionality).

### Power On, No Update

Step                                                                                          | RW  | RB contents | `_AT_BOOT` | `_NOW`
--------------------------------------------------------------------------------------------- | --- | ----------- | ---------- | ------
(initial state)                                                                               | M   | 1/blank     | RO/RW/RB   | RO/RW/RB
1. RO waits 1 second for an update request from AP                                            |     |             |            |
2. RO verifies RW signature => RW is good                                                     |     |             |            |
3. RO notes that `_AT_BOOT` and `_NOW` already protect everything, so no reboot is necessary. |     |             |            |
4. RO jumps to RW                                                                             |     |             |            |

### Updating RW

Assume AP now has a new `BaseEC`-RW, version N>M. The base is already running RW
version M. In this card, the rollback version in both version is identical
("1"), so RB does not require an update.

Step                                                    | RW  | RB contents | `_AT_BOOT`     | `_NOW`
------------------------------------------------------- | --- | ----------- | -------------- | ------
RW is running                                           | M   | 1/blank     | RO/RW/RB       | RO/RW/RB
AP tells RW to prepare for an update (UNLOCK_RW)        |     |             |                |
RW unsets `EC_FLASH_PROTECT_RW_AT_BOOT` to unprotect RW |     |             | **RO/\_\_/RB** |
RW reboots to update `EC_FLASH_PROTECT_RW_NOW`          |     |             |                | **RO/\_\_/RB**

The next base boot is where the update takes place:

Step                                                                                                                                                             | RW    | RB contents | `_AT_BOOT`   | `_NOW`
---------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- | ----------- | ------------ | ------
RO waits 2 seconds for an update request from the AP                                                                                                             | M     | 1/blank     | RO/\_\_/RB   | RO/\_\_/RB
AP tells RO an update is coming (`STOP_IN_RO`)                                                                                                                   |       |             |              |
AP tells the user that a base update is taking place. UI should say: "Please don't be surprised that your keyboard and trackpad won't work for a few seconds..." |       |             |              |
AP writes RW version N                                                                                                                                           | **N** |             |              |
AP tells RO to reboot (`IMMEDIATE_RESET`)                                                                                                                        |       |             |              |
RO reboots, verifies RW signature => RW is good                                                                                                                  |       |             |              |
RO checks RW rollback version N (1) and sees it's greater or equal than RB rollback version 1. So, RW is good.                                                   |       |             |              |
RO sets `RW_AT_BOOT` to protect RW on the next boot.                                                                                                             |       |             | **RO/RW/RB** |
RO reboots                                                                                                                                                       |       |             |              | **RO/RW/RB**

The next base boot is where we first run the new RW firmware.

### Roll forward

Now let's assume we followed the steps above, and we now have a RW version O
that has rollback version 2.

Step                                                                                                           | RW    | RB contents | `_AT_BOOT`     | `_NOW`
-------------------------------------------------------------------------------------------------------------- | ----- | ----------- | -------------- | ------
RO verifies RW signature => RW is good                                                                         | **O** | 1/blank     | RO/RW/RB       | RO/RW/RB
RO checks RW rollback version O (2) and sees it's greater or equal than RB rollback version 1. So, RW is good. |       |             |                |
RO jumps to RW                                                                                                 |       |             |                |
AP is satisfied that the base works, so it tells RW to prepare for a                                           |       |             |                |
roll-forward (`UNLOCK_ROLLBACK`)                                                                               |       |             |                |
RW unsets `ROLLBACK_AT_BOOT`                                                                                   |       |             | **RO/RW/\_\_** |
RW may reboot (or just wait for next reattach)                                                                 |       |             |                | **RO/RW/\_\_**

On next boot, RB will be updated:

Step                                                                                                                                      | RW  | RB contents | `_AT_BOOT`   | `_NOW`
----------------------------------------------------------------------------------------------------------------------------------------- | --- | ----------- | ------------ | ------
RO verifies RW signature => RW is good                                                                                                    | O   | 1/blank     | RO/RW/\_\_   | RO/RW/\_\_
RO sees that RB is unprotected, and sees RW rollback version O (2) and sees is greater than RB rollback version 1. So RB needs an update. |     |             |              |
RO updates RB's second block                                                                                                              | O   | **1/2**     |              |
RO sets `ROLLBACK_AT_BOOT` to protect RB on the next boot.                                                                                |     |             | **RO/RW/RB** |
RO reboots.                                                                                                                               |     |             |              | **RO/RW/RB**

## Details

### STM32 Flash Protection

At a high level, flash protection works on the STM32F072 chip works in the
following manner:

*   128KB flash total flash, organized as 32 independently protectable 4KB
    blocks. Each block has 2 independently erasable 2KB sectors.
*   `FLASH_WRPR` is the register controlling flash write protect of these
    blocks. It is not directly writable. In EC common code, these bits are
    abstracted as `EC_FLASH_PROTECT_[REGION]_NOW`.
*   Instead, there is a non-volatile register called `WRPx`, which is stored in
    a separate information block of flash. This is always writable. In EC common
    code, these bits are abstracted as `EC_FLASH_PROTECT_[REGION]_AT_BOOT`. On
    chip reset, `WRPx` is copied to the `FLASH_WRPR` register.

Here's the interesting part. The only way to change read-only firmware is to
change `WRPx` and then reset the chip, so that `WRPx` is copied into
`FLASH_WRPR`. At that point, read-only firmware could be writable. But that same
reset also transfers control back to the read-only firmware. If the read-only
firmware doesn't want to be writable, all it has to do is change `WRPx` back to
protect itself, and then reboot again. We do that already on all devices which
use the STM32 chips.

Flash protection works similarly on other STM32F chips, if we need to move to a
larger or more capable EC for the base to support a more complex base.

### Flash Contents

The 128KB `BaseEC` flash will be divided into three parts.

*   Read-only firmware (`Base`-EC-RO, or just "RO" in this document)
    *   ~40KB
    *   Minimal functionality, so it can be small.
    *   Verifies the rewritable firmware.
    *   Updates the rewritable firmware over USB.
    *   Does NOT have keyboard or trackpad support.
    *   Includes the `Base-EC` root key.
*   Rewritable firmware (`Base-EC`-RW, or just "RW" in this document)
    *   ~84KB
    *   Supports keyboard and trackpad.
    *   Trackpad drivers may be non-trivial in size.
    *   Future bases may include type-C ports, sensors, or batteries, all of
        which will increase RW size.
    *   As with the main EC, it is unlikely we will have space for multiple
        copies of RW (so, no RW-A and RW-B).
    *   Updates the read-only firmware over USB (pre-production devices only).
*   Rollback block (`Base-EC`-RB, or just "RB" in this document)
    *   4KB (one protection block)
    *   Contains rollback version information for RW
    *   Only writable by RO.
    *   Updates alternate between the 2 2KB erase sectors. We only erase one of
        them at a time, so an interrupted erase/write will not cause data loss.

Adding the RB will decrease the total amount of flash available for RO and RW,
but doesn't require any additional external components. This is acceptable
because RO will be smaller (since it only has update/verify functionality).

### Verification Speed

On a STM32F072 chip running at 48 MHz,

*   SHA-256 of a 64KB RW image takes 200 ms (~3 ms/KB)
    *   Reducing RW image size reduces verification time almost proportionally
        (even if we need to check that the rest of the image is erased).
*   RSA-2048 (exponent 3) signature verification takes ~50 ms
*   RSA-3072 (exponent 3) signature verification takes ~100 ms

[STM32F072]: http://www.st.com/content/ccc/resource/technical/document/reference_manual/c2/f8/8a/f2/18/e6/43/96/DM00031936.pdf/files/DM00031936.pdf
[EC Update over USB]: ./usb_updater.md
[CL:452815]: https://chromium-review.googlesource.com/c/452815/2
