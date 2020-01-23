# Chrome OS Fingerprint Factory Requirements

This document provides an overview of factory requirements and testing for the
fingerprint sensor.

[TOC]

## Contact

For questions regarding this document, please contact the
[Chrome OS Fingerprint Team].

## Terminology

*   `AP`: Application Processor.
*   `FPMCU`: Fingerprint Microcontroller.
*   `FATP`: Final Assembly, Test, and Pack
*   `FP sensor`: Fingerprint sensor. Directly connected to the FPMCU, not the
    AP.
*   `firmware`: Software that runs on the FPMCU.
*   `finalization`: Process that is run in the factory before the device being
    built leaves the factory.
*   `entropy`: Cryptographically secure random bytes stored in FPMCU flash. Used
    for encrypting/decrypting fingerprint templates.
*   `software write protect`: Prevents the RO portion of the FPMCU’s flash from
    being overwritten. Full details in [EC docs][Software Write Protect].
*   `ITS`: In-Device Test Specification.
*   `MTS`: Module Test Specification.
*   `MQT`: Module Quality Test.
*   `MQT2`: Module Quality Test 2.

## Documents

*   [FPC1025: Module Test Specification]
*   [FPC1145: Module Test Specification]
*   [FPC In-Device Test Specification]
*   [Factory Fingerprint Sensor Testing for `nocturne` ]

## FPMCU Firmware Location

The binaries for the FPMCU firmware are located in `/opt/google/biod/fw`. Now
that Chrome OS supports unibuild, there may be multiple firmware binaries in the
directory since multiple sensors may be used across a single "board" (e.g., the
`hatch` board can use either `bloonchipper` or `dartmonkey`).

The correct firmware type to use for a given board can be discovered with the
[Chrome OS Config] tool:

```bash
(dut) $ cros_config /fingerprint board
dartmonkey
```

OR

```bash
(chroot) $  cros_config_host -c /build/<BOARD>/usr/share/chromeos-config/yaml/config.yaml -m <MODEL> get /fingerprint board
dartmonkey
```

The corresponding firmware for the above command would be
`/opt/google/biod/fw/dartmonkey_*.bin`.

*** note
**NOTE**: If you get an empty response when running the above commands, the
Chrome OS Config settings may not have been updated for the Chrome OS board.
See the instructions on [updating Chrome OS Config] for fingerprint.
***

Note that the fingerprint team continuously releases updates to the firmware, so
SIEs should watch for version changes in ToT if they are maintaining a separate
factory branch.

## Flashing the FPMCU

When the FPMCU is completely blank a low-level flashing tool must be used to
program an initial version of the FPMCU firmware. It’s possible to use the
[`flash_fp_mcu`] script as this low-level flashing tool, though since it
requires the AP and is not necessarily robust against failures, it is not
recommended for mass-production. More details about [`flash_fp_mcu`] are in the
[Fingerprint flashing documentation].

The initial version of the FPMCU firmware should be flashed either by the module
house or by the factory. Once an initial version of the FPMCU firmware has been
flashed (i.e., the FPMCU is not blank), the `bio_fw_updater` tool runs on
startup and handles updating the FPMCU firmware to match the version that is in
the rootfs. Note that this update process can take around 30 seconds; if that
length of time is an issue then the factory or module house should pre-flash the
latest firmware beforehand.

*** note
**NOTE**: If the FPMCU is not flashed in the factory as part of development
builds (EVT, etc.), it's possible for developers (or Chromestop) to manually
run [`flash_fp_mcu`], as long as they can disable [hardware write protect].
Obviously this only applies during development, not mass production.
***

## biod and timberslide

Since `biod` communicates with the FPMCU, it’s best to disable it when running
the fingerprint factory tests. This can be done with upstart:

```bash
(dut) $ stop biod
```

Once testing is complete `biod` should be restarted (or you can reboot the
device).

`timberslide` is the daemon that periodically sends commands to the FPMCU to
read the latest FPMCU logs. It writes the results to `/var/log/cros_fp.log`. It
should be fine to leave running during tests, though it should be stopped before
running the [`flash_fp_mcu`] script, since that script erases the entire FPMCU:

```bash
(dut) $ stop timberslide LOG_PATH=/sys/kernel/debug/cros_fp/console_log
```

## Factory Tests

### Fingerprint Sensor (standalone module)

When using an FPC sensor (e.g., FPC 1025, FPC 1145), the fingerprint sensor
itself must be tested by the module manufacturer with FPC’s tools. FPC provides
a Module Test Tool (MTT), which requires additional hardware (FPC Module Test
Card). FPC also provides design drawings for the rubber stamp. The stamp,
test-fixture and test station need to be implemented by the OEM/ODM/Module House
(often only module house).

The `MTS` _must_ be followed by the module manufacturer, but Google does not
provide direct support for this testing. FPC is the main point of contact.

The module testing procedure is documented in the following:

[FPC1025: Module Test Specification]

[FPC1145: Module Test Specification]

### Fingerprint Sensor + FPMCU (in device)

In-device tests are run during the `FATP` process once the device has been fully
assembled. Google provides source code for these tests in
[`fingerprint_mcu.py`].

Hardware Required: Chrome OS DUT before finalization.

Documentation: [FPC In-Device Test Specification]

#### Test Image Checkerboard and Inverted Checkerboard Test (CB/ICB)

##### Purpose

Capture a checkerboard (and inverted checkerboard) pattern and verify that the
values of the individual pixels do not deviate from the median.

##### Implementation

Use `ectool` to capture the first checkerboard pattern image:

```bash
(dut) $ ectool --name=cros_fp fpmode capture pattern0; ectool --name=cros_fp waitevent 5 500
FP mode: (0x20000008) capture
MKBP event 5 data: 00 00 00 80
```

Copy the first checkerboard image to a file:

```bash
(dut) $ ectool --name=cros_fp fpframe > /tmp/pattern0.pnm
```

Use `ectool` to capture the second checkerboard pattern image:

```bash
(dut) $ ectool --name=cros_fp fpmode capture pattern1; ectool --name=cros_fp waitevent 5 500
FP mode: (0x30000008) capture
MKBP event 5 data: 00 00 00 80
```

Copy the second checkerboard image to a different file:

```bash
(dut) $ ectool --name=cros_fp fpframe > /tmp/pattern1.pnm
```

Perform median analysis on the resulting image as described in the `MTS`
document. The factory toolkit does this in
[`fingerprint_mcu.py`][Checkerboard Test].

*** note
**TIP**: You can view the `.pnm` files generated by the commands below on your
Linux desktop with ImageMagick: `display /path/to/file.pnm`.
***

##### Success/Failure

The median pixel value (type 1 and type 2), pixel errors, finger detect zone
errors, and pixel error deviation limit must fall within the acceptance criteria
limits specified in "4.3.5 Acceptance Criteria Test Image CB / iCB" in the `MTS`
document for the sensor being tested.

#### Hardware Reset Test (aka IRQ test)

##### Purpose

Perform a hardware reset of the sensor and test that the IRQ line is asserted
after 5 ms. See "Section 4.1 Reset test pattern procedure" and "2.8 HW Reset" in
the FPC `MTS` document for the sensor being tested.

##### Implementation

This is implemented by the FPMCU on every boot. The results can be checked with
the `ectool` command. The factory toolkit does this in
[`fpmcu_utils.py`][GetSensorIdErrors].

##### Success/Failure

The `Error flags` line of the `fpinfo` `ectool` command must be empty.

```bash
(dut) $ ectool --name=cros_fp fpinfo

Fingerprint sensor: vendor 20435046 product 9 model 1401 version 1
Image: size 56x192 8 bpp
Error flags:
Dead pixels: UNKNOWN
Templates: version 4 size 47616 count 0/5 dirty bitmap 0
```

#### Hardware ID (HWID) check

##### Purpose

Ensure that communications between the sensor and the FPMCU are working and that
the correct sensor has been assembled.

##### Implementation

`ectool` can be used to request the hardware ID, which can be compared with the
expected hardware ID. The factory toolkit does this in
[`fpmcu_utils.py`][GetSensorId].

##### Success/Failure

The `Fingerprint sensor` line of the `fpinfo` `ectool` command must show the
expected ID and the `Error flags` line must be empty:

```bash
(dut) $ ectool --name=cros_fp fpinfo

Fingerprint sensor: vendor 20435046 product 9 model 1401 version 1  # FPC 1145
Image: size 56x192 8 bpp
Error flags:
Dead pixels: UNKNOWN
Templates: version 4 size 47616 count 0/5 dirty bitmap 0
```

#### Reset Pixel (RP)

##### Purpose

Capture a white image, compare the individual pixel values and ensure that the
deviation to the median is within the specified range.

##### Implementation

Capture the test image with `ectool` and analyze the output. The factory toolkit
does this in [`fingerprint_mcu.py`][ProcessResetPixelImage].

Switch to correct capture mode and wait:

```bash
(dut) $ ectool --name=cros_fp fpmode capture test_reset; ectool --name=cros_fp waitevent 5 500
FP mode: (0x50000008) capture
MKBP event 5 data: 00 00 00 80
```

Retrieve the test image:

```bash
(dut) $ ectool --name=cros_fp fpframe > /tmp/test_reset.pnm
```

##### Success/Failure

A pixel is considered to be a bad pixel ("reset pixel error") if the value read
out deviates more than a defined value from the median. The median value and the
max number of pixels that have "reset pixel error" are defined in section "Reset
Pixel" (4.4 or 4.5) of the MTS for the given sensor.

#### Module Quality Test (or Module Quality Test 2) with Rubber Stamp Zebra (Optional)

##### Purpose

The Module Quality Test (`MQT`) uses a rubber stamp with a "zebra" pattern to
characterize module performance and image quality after the top layer (including
stack-up) is applied. Although this test is optional, OEMs are strongly
encouraged to perform it.

##### Implementation

Capture the image when the rubber stamp is applied:

```bash
(dut) $ ectool --name=cros_fp fpmode capture qual
FP mode: (0x40000008) capture
```

Wait for the capture to be finished, timeout after 10s:

```bash
(dut) $ ectool --name=cros_fp waitevent 5 10000
MKBP event 5 data: 00 00 00 80
```

Copy the raw captured from to the AP:

```bash
(dut) $ ectool --name=cros_fp fpframe raw > /tmp/fp.raw
```

Run the analysis tool on the captured frame:

```bash
(dut) $ /usr/local/opt/fpc/fputils.py --mqt /tmp/fp.raw
Error, MQT status : (5)
MQT failed (-1)
```

The factory toolkit does this in [`fingerprint_mcu.py`][rubber_finger_present].

##### Success/Failure

See "Section 5.1.5" Acceptance Criteria for `MQT2` or "Section 5.2.5 Acceptance
Criteria" in the MTS for the given sensor.

## Finalization

The finalization process must perform two tasks:

1.  Initialize the FPMCU’s `entropy`.
1.  When building for PVT or mass production, enable `software write protect`.

### Initialize FPMCU Entropy

The `bio_wash` tool is intended to support both the first time factory
initialization and RMA, depending on the flag. When run with the
`--factory_init` argument (`bio_wash --factory_init`), it will ensure that the
`entropy` is set. If the `entropy` has already been set it will do nothing.

A side-effect of running `bio_wash` is that the `rollback_id` changes (`ectool
--name=cros_fp rollbackinfo`). Initially when the firmware is first flashed, the
`rollback_id` should be zero. After `entropy` is initialized the `rollback_id`
should be set to 1.

Note that for new devices coming out of the factory we expect `rollback_id` to
be 1, which indicates that the entropy has been set exactly once.

### Enable Software Write Protect

`Software write protect` must be enabled for PVT and mass production devices. It
ensures that the RO portion of the FPMCU firmware cannot be overwritten, so it
is critical for FPMCU security.

The following commands will enable software write protection:

```bash
(dut) $ ectool --name=cros_fp flashprotect enable    # enable
(dut) $ sleep 2
(dut) $ ectool --name=cros_fp reboot_ec              # reboot so it takes effect
(dut) $ sleep 2
```

To validate that software write protection has taken effect, run the following:

```bash
(dut) $ ectool --name=cros_fp flashprotect   # get flashprotect state

# output should match below
Flash protect flags: 0x0000000b wp_gpio_asserted ro_at_boot ro_now
Valid flags:         0x0000003f wp_gpio_asserted ro_at_boot ro_now all_now STUCK INCONSISTENT
Writable flags:      0x00000004 all_now
```

If software write protection is not enabled, you will see the following instead:

```bash
(dut) $ ectool --name=cros_fp flashprotect  # get flashprotect state

# not protected
Flash protect flags: 0x00000000
Valid flags:         0x0000003f wp_gpio_asserted ro_at_boot ro_now all_now STUCK INCONSISTENT
Writable flags:      0x00000001 ro_at_boot
```

Capturing a raw frame from the sensor will only work when software write
protection is not enabled, so the test should check the following command works
*before* write protection is enabled and then fails *after* write protection is
enabled:

```bash
(dut) $ ectool --name=cros_fp fpframe raw

# write protection disabled, exit code 0 and output will be raw bytes

# write protection enabled, exit code 1 and output will be
EC result 4 (ACCESS_DENIED)
Failed to get FP sensor frame
```

## RMA Process

As part of the RMA process, the `entropy` needs to be reset so that the new
device owner has a new unique encryption key.

The `bio_wash` tool is intended to support both the first time factory
initialization and RMA, depending on the flag. When run without any arguments
(`bio_wash`), it will forcibly reset the entropy.

The RMA process should either run `bio_wash` without any arguments or re-flash
the FPMCU firmware and then run `bio_wash --factory_init` to make sure that the
entropy has been reset.

## Miscellaneous Commands for Test Implementations

### FPMCU Image Version

```bash
(dut) $ ectool --name=cros_fp version

RO version:    nocturne_fp_v2.2.64-58cf5974e
RW version:    nocturne_fp_v2.2.110-b936c0a3c
Firmware copy: RW
Build info:    nocturne_fp_v2.2.110-b936c0a3c 2018-11-02 14:16:46 @swarm-cros-461
Tool version:  v2.0.2144-1524c164f 2019-09-09 06:50:36 @chromeos-ci-legacy-us-central2-d-x32-7-3ay8
```

### Capture Raw Images

Put your finger on the sensor, then run:

```bash
(dut) $ ectool --name=cros_fp fpmode capture vendor
```

Wait for the capture to be finished, timeout after 10s:

```bash
(dut) $ ectool --name=cros_fp waitevent 5 10000
MKBP event 5 data: 00 00 00 80
```

Remove the finger from the sensor, then start the retrieval of the frame from
the MCU to the AP:

```bash
(dut) $ ectool --name=cros_fp fpframe raw > /tmp/fp.raw
```

To convert the images from FPC’s proprietary format to PNG, you will need to
have `cros deploy`’d `libfputils-nocturne`, which will install the required
utilities in `/opt/fpc`.

*** note
**NOTE**: As of 2019-05-21, the `libfputils` library only works for the FPC 1145
sensor (in nocturne), not the FPC 1025 sensor (hatch).
***

Convert the buffer in proprietary format into png:

```bash
(dut) $ /opt/fpc/fputils.py /tmp/fp.raw --png
Extraction found 2 images
Wrote /tmp/fp.0.png (14085 bytes)
Wrote /tmp/fp.1.png (14025 bytes)
```

[Software Write Protect]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/docs/write_protection.md#Software-Write-Protect
[hardware write protect]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/docs/write_protection.md#hw_wp
[FPC1025: Module Test Specification]: http://go/cros-fingerprint-fpc1025-module-test-spec
[FPC1145: Module Test Specification]: http://go/cros-fingerprint-fpc1145-module-test-spec
[FPC In-Device Test Specification]: http://go/cros-fingerprint-fpc-indevice-test-spec
[`fingerprint_mcu.py`]: https://chromium.googlesource.com/chromiumos/platform/factory/+/master/py/test/pytests/fingerprint_mcu.py
[Checkerboard Test]: https://chromium.googlesource.com/chromiumos/platform/factory/+/d23ebc7eeb074760e8a720e3acac4cfe4073b2ae/py/test/pytests/fingerprint_mcu.py#166
[GetSensorIdErrors]: https://chromium.googlesource.com/chromiumos/platform/factory/+/d23ebc7eeb074760e8a720e3acac4cfe4073b2ae/py/test/utils/fpmcu_utils.py#73
[GetSensorId]: https://chromium.googlesource.com/chromiumos/platform/factory/+/d23ebc7eeb074760e8a720e3acac4cfe4073b2ae/py/test/utils/fpmcu_utils.py#65
[ProcessResetPixelImage]: https://chromium.googlesource.com/chromiumos/platform/factory/+/d23ebc7eeb074760e8a720e3acac4cfe4073b2ae/py/test/pytests/fingerprint_mcu.py#268
[rubber_finger_present]: https://chromium.googlesource.com/chromiumos/platform/factory/+/d23ebc7eeb074760e8a720e3acac4cfe4073b2ae/py/test/pytests/fingerprint_mcu.py#330
[Chrome OS Fingerprint Team]: http://go/cros-fingerprint-docs
[Factory Fingerprint Sensor Testing for `nocturne`]: http://go/fingerprint-factory-testing-nocturne
[`flash_fp_mcu`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/util/flash_fp_mcu
[Fingerprint flashing documentation]: ./fingerprint.md#factory-rma-dev-updates
[Chrome OS Config]: https://chromium.googlesource.com/chromiumos/platform2/+/master/chromeos-config/README.md
[updating Chrome OS Config]: ./fingerprint.md#update-chromeos-config
