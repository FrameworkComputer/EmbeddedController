# ChromeOS Fingerprint Sensor: Quick Factory Guide

The goal of this document is to outline how ODM partners can make use of the
existing ChromeOS factory scripts to meet ChromeOS FPS factory requirements.

[TOC]

## Factory Requirements

### Flash firmware for fingerprint sensor microcontroller (FPMCU)

FPMCU firmware must be flashed before fingerprint functional test is run. ODM
partners may work with the module house to preflash FPMCU firmware before
factory SMT. However, this way ODM partners have to coordinate with the module
house to make sure the preflash FPMCU firmware blob is extracted from the FSI
release image (from `/opt/google/biod/fw/`). If the FPMCU firmware doesn’t match
the FPMCU firmware blob checked into the release image, the end users will see
the ‘critical update’ screen in their out-of-box experience, because
`bio_fw_updater` tries to update FPMCU firmware at boot time. This is a bad user
experience we want to avoid. Most importantly, in PVT/MP build, only the FPMCU
firmware in the release image would be signed by MP key. So you **MUST** ensure
FPMCU is flashed with the MP-signed firmware blob extracted from FSI, before
shipping the devices.

As opposed to pre-flashing FPMCU in the module house, ODM partners are
encouraged to make use of [`update_fpmcu_firmware.py`] to update FPMCU firmware
in the factory flow. This script can detect fingerprint MCU board name, find the
right FPMCU firmware blob for the DUT from the release partition, and then flash
FPMCU by [`flash_fp_mcu`] tool. Please note that this script may take more than
30 secs to complete, which is slow.

Since `bio_fw_updater` has been disabled in factory test image via
[crrev/c/1913645](https://chromium-review.googlesource.com/c/chromiumos/platform2/+/1913645),
in the factory flow, the FPMCU firmware should not be overwritten by
`boot-update-firmware` service during reboot.

### Run fingerprint sensor functional test

Please add [`fingerprint_mcu.py`] to your device test list. A more detailed
description about this test can be found [here][factory requirements].

### Initialize FPMCU entropy in factory finalization

The support for FPMCU entropy initialization has been integrated into the
factory finalization script. So FPMCU entropy should be automatically
initialized in factory finalization, if a FPMCU is found on DUT. Note that FPMCU
entropy initialization would fail if `rollback_block_id` is not equal to zero,
which means the entropy has been initialized before. It is usually caused by
biod trying to initialize FPMCU entropy and increment `rollback_block_id` at
boot time. Since we have disabled biod and `bio_crypto_init` in factory test
image via
[crrev/c/1910290](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1910290),
we expect `rollback_block_id` would stay zero during the factory flow, and FPMCU
entropy initialization should succeed in factory finalization. So just run
factory finalization as any other CrOS boards.

### Enable FPMCU software write protection (SWWP) in factory finalization in PVT/MP

The support for FPMCU SWWP has been integrated into factory finalization script.
So FPMCU SWWP should be automatically enabled in factory finalization together
with AP/EC SWWP when `write_protection` arg is set to `true` and a FPMCU is
found on DUT. Just run factory finalization as any other CrOS boards.

### Reset entropy for factory re-finalization (in case of RMA or OQC)

For the boards that have been finalized, FPMCU entropy has been initialized. So
running re-finalization for those boards are expected to fail at FPMCU entropy
initialization. Before running re-finalization for those boards, ODM partners
have to remove hardware write protection (HWWP) and then run
[`update_fpmcu_firmware.py`] to reset `rollback_block_id` and entropy. So the
follow-up re-finalization (which re-initialize entropy) can succeed.

## References

*   CrOS fingerprint factory requirements: [doc link][factory requirements]
*   The summary of CLs:
    *   Add a factory script to update FPMCU firmware:
        [crrev/c/1918679](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1918679),
        [crrev/c/1913493](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1913493),
        [crrev/c/1927149](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1927149),
        [crrev/c/1984618](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1984618),
        [crrev/c/2036574](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/2036574)
    *   Disable FPS-related services that will interfere with the factory flow:
        [crrev/c/1913645](https://chromium-review.googlesource.com/c/chromiumos/platform2/+/1913645),
        [crrev/c/1910290](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1910290)
    *   Support FPMCU in factory finalization:
        [crrev/c/1868795](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1868795),
        [crrev/c/1902267](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1902267),
        [crrev/c/1900503](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1900503),
        [crrev/c/1925927](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1925927),
        [crrev/c/1948163](https://chromium-review.googlesource.com/c/chromiumos/platform/factory/+/1948163)

[`update_fpmcu_firmware.py`]: https://crsrc.org/o/src/platform/factory/py/test/pytests/update_fpmcu_firmware.py;drc=672e24bb3e2dd0dec7578dcd4c52805d022662d1
[factory requirements]: ./fingerprint-factory-requirements.md
[`fingerprint_mcu.py`]: https://crsrc.org/o/src/platform/factory/py/test/pytests/fingerprint_mcu.py;drc=672e24bb3e2dd0dec7578dcd4c52805d022662d1
[`flash_fp_mcu`]: https://crsrc.org/o/src/platform/ec/util/flash_fp_mcu;drc=12c473337e3bdcce6d180d266c9e9f8127448f33
