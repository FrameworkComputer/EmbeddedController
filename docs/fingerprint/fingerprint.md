# Fingerprint Firmware (FPMCU)

[TOC]

<!-- mdformat off(b/139308852) -->
*** note
NOTE: The build commands assume you are in the `~/trunk/src/platform/ec`
directory inside the chroot.
***
<!-- mdformat on -->

<!-- mdformat off(b/139308852) -->
*** note
WARNING: When switching branches in the EC codebase, you probably want to nuke
the `build` directory or at least the board you're working on: `rm -rf
build/<board>` or `make clobber` to prevent compilation errors.
***
<!-- mdformat on -->

## Software

The main source code for fingerprint sensor functionality lives in the
[`common/fpsensor`] directory. The driver code for specific sensors lives in the
[`driver/fingerprint`] directory.

## Hardware {#hardware}

The following "boards" (specified by the `BOARD` environment variable when
building the EC code) are for fingerprint:

MCU                      | Sensor     | Firmware (EC "board")                          | Dev Board                                  | Nucleo Board
------------------------ | ---------- | ---------------------------------------------- | ------------------------------------------ | ------------
[STM32H743] \(Cortex-M7) | [FPC 1145] | `dartmonkey`<br>(aka `nocturne_fp`, `nami_fp`) | [Icetower v3] <br>(Previously Dragontalon) | [Nucleo H743ZI2]
[STM32F412] \(Cortex-M4) | [FPC 1025] | `bloonchipper`<br>(aka `hatch_fp`)             | [Dragonclaw v4]                            | [Nucleo F412ZG]
NPCX99FP \(Cortex-M4)    | [FPC 1025] | `helipilot`                                    | [Quincy v3]                                | N/A

RAM and Flash details for each board are in the [Fingerprint MCU RAM and Flash]
document.

### Sensor Template Sizes

Sensor     | Fingerprint Template Size
---------- | --------------------------------
[FPC 1145] | [~48 KB][FPC 1145 Template Size]
[FPC 1025] | [~5 KB][FPC 1025 Template Size]

### Determining Hardware {#chromeos-config-fingerprint}

If you have access to a shell on your Chromebook, you can use [ChromeOS Config]
to determine the FPMCU that it contains:

```bash
(dut) $ cros_config /fingerprint board
```

Alternatively, if you have a Chromium OS build, you can use [ChromeOS Config] in
the chroot to determine the FPMCU:

```bash
(chroot) $  cros_config_host -c /build/<BOARD>/usr/share/chromeos-config/yaml/config.yaml -m <MODEL> get /fingerprint board
```

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: If you get an empty response when running these commands, the
[ChromeOS Config] properties for fingerprint may not have been set up yet. See
the [section on updating ChromeOS Config](#update-chromeos-config).
***
<!-- mdformat on -->

## Building FPMCU Firmware Locally

### See `Makefile` target options

```bash
(chroot) ~/trunk/src/platform/ec $ make help
```

### Build

Replace `<BOARD_NAME>` in the command below with the fingerprint MCU that you
are targeting (e.g., `nocturne_fp`, `dartmonkey`, `bloonchipper`).

```bash
(chroot) ~/trunk/src/platform/ec $ make BOARD=<BOARD_NAME> -j
```

### Verbose Build output

Use `V=1` to see the complete compiler output (all flags).

```bash
(chroot) ~/trunk/src/platform/ec $ make V=1 BOARD=nocturne_fp -j
```

## Building all EC firmware (before "repo upload")

Before uploading a change to Gerrit via `repo upload`, you'll need to build
*all* the boards in the EC codebase to make sure your changes do not break any
others.

<!-- mdformat off(b/139308852) -->
*** note
NOTE: If you forget to do this, do not worry. `repo upload` will warn you and
prevent you from uploading.
***
<!-- mdformat on -->

```bash
(chroot) ~/trunk/src/platform/ec $ make buildall -j
```

## Building and running unit tests

See the [Unit Tests] documentation for details on how to [run the unit tests].

## Build ectool

```bash
(chroot) ~/trunk/src/platform/ec $ make BOARD=nocturne_fp utils-host -j
```

## Build and run the `host_command` fuzz test

<!-- mdformat off(b/139308852) -->
*** note
NOTE: For more details on fuzzing, see [Fuzz Testing in ChromeOS].
***
<!-- mdformat on -->

```bash
(chroot) ~/trunk/src/platform/ec $ make run-host_command_fuzz
```

## Logs

[`timberslide`] is a simple daemon that collects logs from the FPMCU and writes
them to disk. [`timberslide`] reads from sysfs, where the kernel driver
[periodically dumps the FPMCU console output][cros_ec_debugfs]. [`timberslide`]
writes the resulting logs to `/var/log/cros_fp.log`. There are multiple
instances of [`timberslide`] that run; one for each MCU running the EC codebase.

### Starting timberslide

```bash
(dut)$ start timberslide LOG_PATH=/sys/kernel/debug/cros_fp/console_log
```

### Stopping timberslide

```bash
(dut)$ stop timberslide LOG_PATH=/sys/kernel/debug/cros_fp/console_log
```

### Manually running timberslide

```bash
(dut)$ timberslide --device_log=/sys/kernel/debug/cros_fp/console_log
```

### Reading logs from kernel

If [`timberslide`] is not running you can just `cat` the logs directly from the
kernel:

```bash
(dut)$ cat /sys/kernel/debug/cros_fp/console_log
```

## Production Updates (Auto-Update)

### `fp_updater.sh` and `bio_fw_updater`

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: The auto-update process requires a working version of the firmware
running on the FPMCU. See [Fingerprint Factory Requirements] for details on
flashing in the factory.
***
<!-- mdformat on -->

[`fp_updater.sh`] and [`bio_fw_updater`] are wrappers around [`flashrom`] and
require already-functioning RO firmware running on the FPMCU. It’s meant to be
used in production to update the RW firmware. `fp_updater.sh` was used prior to
M77; `bio_fw_updater` replaces it.

It's also possible to use the updater to update the RO firmware if you disable
*both* HW and SW write protect, which we use for updating development devices
that do not have write protect enabled (dogfood devices, EVT, etc.)

In production, only the RW portion of the firmware can be updated (unless the
user disables [hardware write protection]).

## Factory / RMA / Development Updates {#factory-rma-dev-updates}

### `flash_fp_mcu`

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: This tool is really just for us to use during development or during
the RMA flow (must go through finalization again in that case). We never update
RO in the field (can’t by design). See [Fingerprint Factory Requirements] for
details on flashing in the factory.
***
<!-- mdformat on -->

[`flash_fp_mcu`] enables spidev and toggles some GPIOs to put the FPMCU (STM32)
into bootloader mode. At that point it uses [`stm32mon`] to rewrite the entire
flash (both RO and RW). The FPMCU can only be put into bootloader mode when
[hardware write protection] is disabled, which means [`flash_fp_mcu`] can only
be used when [hardware write protection] is disabled.

[`flash_fp_mcu`] is available in the [Chromium OS test image].

### `stm32mon`

[`stm32mon`] is a tool used to send commands to the STM32 bootloader. We use it
for development (through [`flash_fp_mcu`]) to erase and flash the entire chip.

[`stm32mon`] is available in the [Chromium OS test image].

## Keys

The `RO` section of the fingerprint firmware contains the public portion of the
key used to sign the RW firmware. The RO firmware uses the public key to
validate the signature of the RW firmware before jumping to it. It is not
possible to update the public key stored in the RO firmware once a device has
been shipped (i.e., once [hardware write protection] is enabled).

Different keys are used to sign the firmware during development and production.
The `dev` key is used for local builds and development and is not private; it is
called `dev_key.pem` and located in the "board" directory for the given FPMCU
(e.g., [`board/nocturne_fp/dev_key.pem`]). After doing a build, the `ec.bin` in
the `build` directory (e.g., `build/nocturne_fp/ec.bin`) will be signed with the
`dev` key.

The two other types of keys are `premp` and `mp`, which stand for "pre-mass
production" and "mass production", respectively. Both the `premp` and `mp` keys
are only available to the buildbots as part of the official build. The `premp`
is typically used during bringup of new hardware to validate the signing flow of
the buildbots, while the `mp` key is used for PVT and production devices.

Switching keys is only possible when the `RO` firmware is not write protected,
since the public portion of the keypair is stored in the `RO` firmware.

### Generate Key

For testing, you can generate a new key by using the following openssl command:

```bash
openssl genrsa -3 -out board/$BOARD/dev_key.pem 3072
```

### Resources

*   http://go/cros-signer-docs
*   https://issuetracker.google.com/issues/77882970

## Signing

[`futility`] is used to sign EC firmware. There’s a wrapper script around it for
signing called [`sign_official_build.sh`].

### Key ID

The output of `futility show` will show a `Public Key File` and `Signature`
section, each of which have an `ID` field. This ID lets you match the key to the
signature in case there is more than one.
[It’s just a sha1sum of the public key,][vboot_key_id] so it lets you
[uniquely identify the key being used][vb2_public_key].

If you have the key (e.g., in PEM format), you can compute the `ID` with the
`futility show` command:

```bash
(chroot) $ futility show ./path/to/key.pem
```

#### Example

If you are building the `hatch_fp` "board" on your local machine (which signs
the resulting `ec.bin` with the `dev` key, you can check the `ID` with:

```bash
(chroot)$ futility show board/hatch_fp/dev_key.pem
```

```
Private Key file:      board/hatch_fp/dev_key.pem
  Key length:          3072
  Key sha1sum:         61382804da86b4156d666cc9a976088f8b647d44
```

```bash
(chroot)$ futility show build/hatch_fp/ec.bin
```

```
Public Key file:       build/hatch_fp/ec.bin
  Vboot API:           2.1
  Desc:                ""
  Signature Algorithm: 7 RSA3072EXP3
  Hash Algorithm:      2 SHA256
  Version:             0x00000001
  ID:                  61382804da86b4156d666cc9a976088f8b647d44
Signature:             build/hatch_fp/ec.bin
  Vboot API:           2.1
  Desc:                ""
  Signature Algorithm: 7 RSA3072EXP3
  Hash Algorithm:      2 SHA256
  Total size:          0x1b8 (440)
  ID:                  61382804da86b4156d666cc9a976088f8b647d44
  Data size:           0x2864c (165452)
Signature verification succeeded.
```

### Showing Key ID (fingerprint) for running FW

[Asked on chromeos-chatty-firmware][chatty-firmware-q] about adding an EC
command to show the Key ID (fingerprint) from the RO version. This would make it
a lot easier during both development and testing.

## Power

See [Measuring Power] for instructions on how to measure power with the
fingerprint development boards.

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: Make sure that any debuggers are completely disconnected when
measuring power.
***
<!-- mdformat on -->

### Dragonclaw v4

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: Make sure that any debuggers are completely disconnected when
measuring power.
***
<!-- mdformat on -->

```bash
(chroot) $ dut-control -t 60 ppvar_fp_mw ppvar_fp_mv ppvar_mcu_mw ppvar_mcu_mv pp1800_fp_mw pp1800_fp_mv
```

**Firmware Version**:
`bloonchipper_v2.0.4277-9f652bb3-RO_v2.0.21769-3757a66-RW.bin`

#### MCU is idle

```
(chroot) $ dut-control fpmcu_slp:off
```

```
@@           NAME  COUNT    AVERAGE  STDDEV        MAX        MIN
@@   sample_msecs   2379    25.2017  3.1648    38.4002    19.7706
@@   pp1800_fp_mv   2379  1792.0303  0.4911  1800.0000  1792.0000
@@   pp1800_fp_mw   2379     0.0000  0.0000     0.0000     0.0000
@@    ppvar_fp_mv   2379  3268.7314  1.6473  3272.0000  3264.0000
@@    ppvar_fp_mw   2379     0.7081  0.5254     3.1751     0.1000
@@   ppvar_mcu_mv   2379  3256.0000  0.0000  3256.0000  3256.0000
@@   ppvar_mcu_mw   2379    16.0525  0.1395    17.1917    15.8893
```

#### MCU in low power mode (suspend)

```
(chroot) $ dut-control fpmcu_slp:on
```

```
@@           NAME  COUNT    AVERAGE  STDDEV        MAX        MIN
@@   sample_msecs   2359    25.4208  3.2038    38.8474    19.5994
@@   pp1800_fp_mv   2359  1792.0712  0.7514  1800.0000  1792.0000
@@   pp1800_fp_mw   2359     0.0000  0.0000     0.0000     0.0000
@@    ppvar_fp_mv   2359  3270.5553  1.9529  3276.0000  3268.0000
@@    ppvar_fp_mw   2359     0.6903  0.5118     3.1751     0.1000
@@   ppvar_mcu_mv   2359  3256.0000  0.0000  3256.0000  3256.0000
@@   ppvar_mcu_mw   2359     0.5296  0.3512     7.0330     0.2605
```

### Icetower v3

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: Before https://crrev.com/c/2689101, the sleep GPIOs were not
configured correctly, so the change needs to be cherry-picked in order to
measure releases before that point.
***
<!-- mdformat on -->

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: Make sure that any debuggers are completely disconnected when
measuring power.
***
<!-- mdformat on -->

```bash
(chroot) $ dut-control -t 60 ppvar_fp_mw ppvar_fp_mv ppvar_mcu_mw ppvar_mcu_mv pp1800_fp_mw pp1800_fp_mv
```

**Firmware Version**:
`dartmonkey_v2.0.2887-311310808-RO_v2.0.21761-e1e012ee3-RW.bin`

#### MCU is idle

```
(chroot) $ dut-control fpmcu_slp:off
```

```
@@           NAME  COUNT    AVERAGE  STDDEV        MAX        MIN
@@   sample_msecs   1047    57.2882  6.6254    78.7930    43.7489
@@   pp1800_fp_mv   1047  1809.7788  0.3202  1810.5469  1808.5938
@@   pp1800_fp_mw   1047     1.1289  0.0526     1.2805     0.9614
@@    ppvar_fp_mv   1047  3238.2607  0.3740  3239.2578  3237.3047
@@    ppvar_fp_mw   1047     0.0331  0.0523     0.2764     0.0000
@@   ppvar_mcu_mv   1047  3238.4156  0.3826  3239.2578  3237.3047
@@   ppvar_mcu_mw   1047    42.6689  0.3530    43.5451    41.6941

```

#### MCU in low power mode (suspend)

```
(chroot) $ dut-control fpmcu_slp:on
```

```
@@           NAME  COUNT    AVERAGE  STDDEV        MAX        MIN
@@   sample_msecs   1032    58.1142  6.0990    74.6982    43.6194
@@   pp1800_fp_mv   1032  1809.7634  0.3269  1810.5469  1809.0820
@@   pp1800_fp_mw   1032     1.1320  0.0529     1.2819     0.9604
@@    ppvar_fp_mv   1032  3243.6192  0.4019  3245.1172  3242.1875
@@    ppvar_fp_mw   1032     0.0324  0.0524     0.3164     0.0000
@@   ppvar_mcu_mv   1032  3243.7285  0.4072  3244.6289  3241.2109
@@   ppvar_mcu_mw   1032     4.4560  0.2319     5.4980     2.5824
```

## ChromeOS Build (portage / ebuild)

In order to use the fingerprint sensor with a given [ChromeOS board], a few
things need to be configured for the [ChromeOS board].

### Enable biod USE flag

The biod [`USE` flag] needs to be enabled for the [ChromeOS board]. This `USE`
flag
[determines whether the `biod` daemon is built and installed][biod chromium-os].

To enable the `USE` flag, update the `make.defaults` for the [ChromeOS board].
See the [`make.defaults` for the Hatch board][hatch make.defaults] as an
example.

#### Verifying biod is installed in the rootfs

After enabling the `biod` [`USE` flag] and building the `biod` package for your
target [ChromeOS board], the `biod` binary should be in the build directory:

```bash
(chroot) $ emerge-<BOARD> biod
```

```bash
(chroot) $ ls /build/<BOARD>/usr/bin/biod
/build/<BOARD>/usr/bin/biod
```

### Update FPMCU_FIRMWARE

`FPMCU_FIRMWARE` should be set to the set of fingerprint firmware that should be
built and installed for the [ChromeOS board].

`FPMCU_FIRMWARE` is a [`USE_EXPAND` variable][`USE` flag],
[defined in the base `make.defaults`][FPMCU_FIRMWARE make.defaults].

The `biod` ebuild uses the resulting [`USE` flags] to
[determine which FPMCU release firmware to build][biod release firmware] and the
[`chromeos-firmware-fpmcu` ebuild] uses the resulting [`USE` flags] to
[determine which firmware to install][firmware ebuild] to the rootfs in
`/opt/google/biod/fw`.

Possible values for `FPMCU_FIRMWARE` can be found by looking at the
`FIRMWARE_EC_BOARD` values in the [`chromeos-fpmcu-release*` ebuilds], which
correspond to the [FPMCU hardware](#hardware).

See the [Hatch baseboard `make.defaults`] for an example.

#### Verifying FPMCU firmware is installed in the rootfs

Once you have added the `FPMCU_FIRMWARE` flag and rebuilt the
[`chromeos-firmware-fpmcu` ebuild], the firmware will show up in the the chroot:

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: This requires access to the [internal manifest].
***
<!-- mdformat on -->

```bash
(chroot) $ emerge-<BOARD> chromeos-firmware-fpmcu
```

```bash
(chroot) $ ls /build/<BOARD>/opt/google/biod/fw
bloonchipper_v2.0.2626-3c315108.bin  dartmonkey_v2.0.2887-311310808.bin
```

The above output assumes you selected the `bloonchipper` and `dartmonkey`
firmware by setting `FPMCU_FIRMWARE="bloonchipper dartmonkey"`. The actual
version numbers displayed will not necessarily match since the firmware is
constantly updated.

### Update ChromeOS Config {#update-chromeos-config}

With "unibuild", the same OS image (build) for a given [ChromeOS board] is used
across multiple devices. Often there will be some devices that have a
fingerprint sensor, some that do not, and even different sensors for the same
board.

Determining what fingerprint hardware is on a given [ChromeOS board] is thus
done at runtime, using [ChromeOS Config].

The `fingerprint` config needs to be in the `model.yaml` for the given
[ChromeOS board]. The [ChromeOS Config fingerprint] section describes the
attributes for the `fingerprint` config in more detail.

The [`ec_extras` attribute] needs to be set to the list of fingerprint firmware
that should be built as part of the build.

See the [`model.yaml` for the Hatch board][hatch model.yaml] as an example.

Instead of crafting the `model.yaml` by hand, newer boards are moving to the
[ChromeOS Project Configuration] model, where the config is generated using
[Starlark]. The common [`create_fingerprint`] function can be used across models
to configure the fingerprint settings. See the [Morphius `config.star`] for an
example of how to call `create_fingerprint`. After you modify a `config.star`
file you will need to [regenerate the config]. If you need to change many
projects (e.g., modifying [`create_fingerprint`]), you can use the [`CLFactory`]
tool.

Once you have updated the config, you can test your changes by
[running `cros_config`](#chromeos-config-fingerprint). The ChromeOS Config
documentation has a [section on testing properties] that describes this in more
detail.

### SKUs

The fingerprint sensor may only be included on certain SKUs for a given device.
The fingerprint code uses [ChromeOS Config] to determine whether a device has a
fingerprint sensor or not. For each SKU, there is an associated
[fingerprint config][ChromeOS Config fingerprint]. [ChromeOS Config] determines
the [SKU information][ChromeOS Config SKU] (and thus the
[fingerprint config][ChromeOS Config fingerprint]) from [CBI Info]. The SKU for
a given device can be found by viewing `chrome://system/#platform_identity_sku`.

## Kernel Driver

The kernel driver responsible for handling communication between the AP and
FPMCU is called [`cros_ec`] and is enabled with [`CONFIG_CROS_EC`] in the Linux
kernel. FPMCUs that are connected via SPI use [`cros_ec_spi.c`], while FPMCUs
that are connected via UART use [`cros_ec_uart.c`].

[`common/fpsensor`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/common/fpsensor/
[`driver/fingerprint`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/driver/fingerprint
[`nocturne_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/nocturne_fp/
[`nami_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/nami_fp/
[`hatch_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/hatch_fp/
[`bloonchipper`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/bloonchipper/
[`dartmonkey`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/dartmonkey/
[hardware write protection]: ../write_protection.md
[`flash_fp_mcu`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/util/flash_fp_mcu
[`stm32mon`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/e1f3f89e7ea7945adddd0c2e6838f5e59856cff2/util/stm32mon.c#14
[`futility`]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/HEAD/futility/
[`sign_official_build.sh`]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/HEAD/scripts/image_signing/sign_official_build.sh
[vboot_key_id]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/e7db36856ce418552637d1981c173d22dfe5bf39/firmware/2lib/include/2id.h#5
[vb2_public_key]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/e7db36856ce418552637d1981c173d22dfe5bf39/firmware/2lib/include/2rsa.h#14
[chatty-firmware-q]: https://groups.google.com/a/google.com/d/msg/chromeos-chatty-firmware/ZSg423wsFPg/26UbdGwjFQAJ
[`fp_updater.sh`]: http://go/cros-fp-updater-nocturne-source
[`bio_fw_updater`]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/tools
[`flashrom`]: https://chromium.googlesource.com/chromiumos/third_party/flashrom/
[STM32F412]: https://www.st.com/resource/en/reference_manual/dm00180369.pdf
[STM32H743]: https://www.st.com/resource/en/reference_manual/dm00314099.pdf
[`board/nocturne_fp/dev_key.pem`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/nocturne_fp/dev_key.pem
[`timberslide`]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/timberslide
[cros_ec_debugfs]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/9db44685934a2e4bc9180ea2de87a6c429672395/drivers/platform/chrome/cros_ec_debugfs.c
[Fingerprint Factory Requirements]: ./fingerprint-factory-requirements.md
[Chromium OS test image]: https://chromium.googlesource.com/chromiumos/platform/factory/+/HEAD/README.md#building-test-image
[ChromeOS Config]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md
[ChromeOS Config fingerprint]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md#fingerprint
[section on testing properties]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md#adding-and-testing-new-properties
[ChromeOS board]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Select-a-board
[biod chromium-os]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/virtual/target-chromium-os/target-chromium-os-1.ebuild#154
[hatch make.defaults]: https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/2f075f0e7ce09d3eb460f3c529da463a6201276c/overlay-hatch/profiles/base/make.defaults#22
[Hatch baseboard `make.defaults`]: https://chrome-internal.googlesource.com/chromeos/overlays/baseboard-hatch-private/+/HEAD/profiles/base/make.defaults#17
[hatch model.yaml]: https://chrome-internal.googlesource.com/chromeos/overlays/overlay-hatch-private/+/HEAD/chromeos-base/chromeos-config-bsp-hatch-private/files/model.yaml
[`ec_extras` attribute]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md#build_targets
[FPMCU_FIRMWARE make.defaults]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/profiles/base/make.defaults#157
[`USE` flag]: https://devmanual.gentoo.org/general-concepts/use-flags/index.html
[`USE` flags]: https://devmanual.gentoo.org/general-concepts/use-flags/index.html
[biod release firmware]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/chromeos-base/biod/biod-9999.ebuild#49
[`chromeos-firmware-fpmcu` ebuild]: https://chrome-internal.googlesource.com/chromeos/overlays/chromeos-overlay/+/HEAD/chromeos-base/chromeos-firmware-fpmcu/chromeos-firmware-fpmcu-9999.ebuild
[firmware ebuild]: https://chrome-internal.googlesource.com/chromeos/overlays/chromeos-overlay/+/HEAD/chromeos-base/chromeos-firmware-fpmcu/chromeos-firmware-fpmcu-9999.ebuild#40
[`chromeos-fpmcu-release*` ebuilds]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/HEAD/sys-firmware
[internal manifest]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#get-the-source-code
[Unit Tests]: ../unit_tests.md
[run the unit tests]: ../unit_tests.md#running
[Measuring Power]: ./fingerprint-dev-for-partners.md#measure-power
[dragonclaw]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
[FPC 1145]: ../../driver/fingerprint/fpc/libfp/fpc1145_private.h
[FPC 1025]: ../../driver/fingerprint/fpc/bep/fpc1025_private.h
[FPC 1145 Template Size]: https://chromium.googlesource.com/chromiumos/platform/ec/+/127521b109be8aac352e80e319e46ed123360408/driver/fingerprint/fpc/libfp/fpc1145_private.h#46
[FPC 1025 Template Size]: https://chromium.googlesource.com/chromiumos/platform/ec/+/127521b109be8aac352e80e319e46ed123360408/driver/fingerprint/fpc/bep/fpc1025_private.h#44
[Dragonclaw v4]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
[Icetower v3]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
[Quincy v3]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
[Nucleo F412ZG]: https://www.digikey.com/en/products/detail/stmicroelectronics/NUCLEO-F412ZG/6137573
[Nucleo H743ZI2]: https://www.digikey.com/en/products/detail/stmicroelectronics/NUCLEO-H743ZI2/10130892
[CBI Info]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
[ChromeOS Config SKU]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md#identity
[ChromeOS Project Configuration]: https://chromium.googlesource.com/chromiumos/config/+/HEAD/README.md
[Starlark]: https://docs.bazel.build/versions/main/skylark/language.html
[`create_fingerprint`]: https://chromium.googlesource.com/chromiumos/config/+/e1fa0d7f56eb3dd6e9378e4326de086ada46b7d3/util/hw_topology.star#444
[Morphius `config.star`]: https://chrome-internal.googlesource.com/chromeos/project/zork/morphius/+/593b657a776ed6b320c826916adc9cd845faf709/config.star#85
[regenerate the config]: https://chromium.googlesource.com/chromiumos/config/+/HEAD/README.md#making-configuration-changes-for-your-project
[`CLFactory`]: https://chromium.googlesource.com/chromiumos/config/+/HEAD/README.md#making-bulk-changes-across-repos
[Fingerprint MCU RAM and Flash]: ./fingerprint-ram-and-flash.md
[`CONFIG_CROS_EC`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/upstream/drivers/platform/chrome/Makefile;l=11;drc=a4e493ca59115fc0692151c1818e5aadf0e79ad0
[`cros_ec`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/upstream/drivers/platform/chrome/cros_ec.c
[`cros_ec_spi.c`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/upstream/drivers/platform/chrome/cros_ec_spi.c
[`cros_ec_uart.c`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/kernel/upstream/drivers/platform/chrome/cros_ec_uart.c
[Fuzz Testing in ChromeOS]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/testing/fuzzing.md
