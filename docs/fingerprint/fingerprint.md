# Fingerprint Firmware (FPMCU)

[TOC]

*** note
NOTE: The build commands assume you are in the `~/trunk/src/platform/ec`
directory inside the chroot.
***

*** note
WARNING: When switching branches in the EC codebase, you probably want to nuke
the `build` directory or at least the board you're working on: `rm -rf
build/<board>` or `make clobber` to prevent compilation errors.
***

## Software

The main source code for fingerprint sensor functionality lives in the
[`common/fpsensor`] directory. The driver code for specific sensors lives in the
[`driver/fingerprint`] directory.

## Hardware {#hardware}

The following "boards" (specified by the `BOARD` environment variable when
building the EC code) are for fingerprint:

MCU                   | Firmware (EC "board")                          | Dev Board
--------------------- | ---------------------------------------------- | ---------
[STM32H743] (Cortex-M7) | `dartmonkey`<br>(aka `nocturne_fp`, `nami_fp`) | Icetower v0.2 <br>(Previously Dragontalon)
[STM32F412] (Cortex-M4) | `bloonchipper`<br>(aka `hatch_fp`)             | Dragonclaw v0.2

### Determining Hardware {#chromeos-config-fingerprint}

If you have access to a shell on your Chromebook, you can use [Chrome OS Config]
to determine the FPMCU that it contains:

```bash
(dut) $ cros_config /fingerprint board
```

Alternatively, if you have a Chromium OS build, you can use [Chrome OS Config]
in the chroot to determine the FPMCU:

```bash
(chroot) $  cros_config_host -c /build/<BOARD>/usr/share/chromeos-config/yaml/config.yaml -m <MODEL> get /fingerprint board
```

*** note
**NOTE**: If you get an empty response when running these commands, the
[Chrome OS Config] properties for fingerprint may not have been set up yet. See
the [section on updating Chrome OS Config](#update-chromeos-config).
***

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

*** note
NOTE: If you forget to do this, do not worry. `repo upload` will warn you and
prevent you from uploading.
***

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

*** note
**NOTE**: The auto-update process requires a working version of the firmware
running on the FPMCU. See [Fingerprint Factory Requirements] for details on
flashing in the factory.
***

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

*** note
**NOTE**: This tool is really just for us to use during development or during
the RMA flow (must go through finalization again in that case). We never update
RO in the field (can’t by design). See [Fingerprint Factory Requirements] for
details on flashing in the factory.
***

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

*   https://sites.google.com/a/google.com/chromeos/resources/engineering/releng/signer-documentation
*   https://sites.google.com/a/google.com/chromeos/paygen---payload
*   https://b.corp.google.com/issues/77882970

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

### Dragonclaw v0.2

```bash
(chroot) $  dut-control -t 60 pp3300_dx_mcu_mv pp3300_dx_fp_mv pp1800_dx_fp_mv pp3300_dx_mcu_mw pp3300_dx_fp_mw pp1800_dx_fp_mw
```

**Firmware Version**: `bloonchipper_v2.0.4277-9f652bb3`

```
@@               NAME  COUNT  AVERAGE  STDDEV      MAX      MIN
@@       sample_msecs    128   469.05   33.79   641.75   399.90
@@    pp1800_dx_fp_mv    128  1802.06    3.50  1808.00  1800.00
@@    pp1800_dx_fp_mw    128     0.00    0.00     0.00     0.00
@@    pp3300_dx_fp_mv    128  3296.00    0.00  3296.00  3296.00
@@    pp3300_dx_fp_mw    128     0.00    0.03     0.26     0.00
@@   pp3300_dx_mcu_mv    128  3288.00    0.00  3288.00  3288.00
@@   pp3300_dx_mcu_mw    128    24.20    0.00    24.20    24.20
```

### Dragontalon

*** note
**NOTE**: The sensor doesn't work on Dragontalon, so the measurements below show
zero for the sensor.
***

```bash
(chroot) $  dut-control -t 60 pp3300_h7_mv pp3300_h7_mw pp1800_fpc_mv pp1800_fpc_mw
```

**Firmware Version**: `dartmonkey_v2.0.4017-9c45fb4b3`

```
@@            NAME  COUNT  AVERAGE  STDDEV      MAX      MIN
@@    sample_msecs   1502    39.96   13.14   379.43    22.31
@@   pp1800_fpc_mv   1502     0.00    0.00     0.00     0.00
@@   pp1800_fpc_mw   1502     0.00    0.00     0.00     0.00
@@    pp3300_h7_mv   1502  3288.00    0.00  3288.00  3288.00
@@    pp3300_h7_mw   1502     8.20    0.51    18.08     7.67
```

## Chrome OS Build (portage / ebuild)

In order to use the fingerprint sensor with a given [Chrome OS board], a few
things need to be configured for the [Chrome OS board].

### Enable biod USE flag

The biod [`USE` flag] needs to be enabled for the [Chrome OS board]. This `USE`
flag
[determines whether the `biod` daemon is built and installed][biod chromium-os].

To enable the `USE` flag, update the `make.defaults` for the [Chrome OS board].
See the [`make.defaults` for the Hatch board][hatch make.defaults] as an
example.

#### Verifying biod is installed in the rootfs

After enabling the `biod` [`USE` flag] and building the `biod` package for your
target [Chrome OS board], the `biod` binary should be in the build directory:

```bash
(chroot) $ emerge-<BOARD> biod
```

```bash
(chroot) $ ls /build/<BOARD>/usr/bin/biod
/build/<BOARD>/usr/bin/biod
```

### Update FPMCU_FIRMWARE

`FPMCU_FIRMWARE` should be set to the set of fingerprint firmware that should be
built and installed for the [Chrome OS board].

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

*** note
**NOTE**: This requires access to the [internal manifest].
***

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

### Update Chrome OS Config {#update-chromeos-config}

With "unibuild", the same OS image (build) for a given [Chrome OS board] is used
across multiple devices. Often there will be some devices that have a
fingerprint sensor, some that do not, and even different sensors for the same
board.

Determining what fingerprint hardware is on a given [Chrome OS board] is thus
done at runtime, using [Chrome OS Config].

The `fingerprint` config needs to be in the `model.yaml` for the given
[Chrome OS board]. The [Chrome OS Config fingerprint] section describes the
attributes for the `fingerprint` config in more detail.

The [`ec_extras` attribute] needs to be set to the list of fingerprint firmware
that should be built as part of the build.

See the [`model.yaml` for the Hatch board][hatch model.yaml] as an example.

You can test your changes by
[running `cros_config`](#chromeos-config-fingerprint). The Chrome OS Config
documentation has a [section on testing properties] that describes this in more
detail.

[`common/fpsensor`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/common/fpsensor/
[`driver/fingerprint`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/driver/fingerprint
[`nocturne_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/board/nocturne_fp/
[`nami_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/board/nami_fp/
[`hatch_fp`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/board/hatch_fp/
[`bloonchipper`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/board/bloonchipper/
[`dartmonkey`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/master/board/dartmonkey/
[hardware write protection]: ../write_protection.md
[`flash_fp_mcu`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/util/flash_fp_mcu
[`stm32mon`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/e1f3f89e7ea7945adddd0c2e6838f5e59856cff2/util/stm32mon.c#14
[`futility`]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/master/futility/
[`sign_official_build.sh`]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/master/scripts/image_signing/sign_official_build.sh
[vboot_key_id]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/e7db36856ce418552637d1981c173d22dfe5bf39/firmware/2lib/include/2id.h#5
[vb2_public_key]: https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/e7db36856ce418552637d1981c173d22dfe5bf39/firmware/2lib/include/2rsa.h#14
[chatty-firmware-q]: https://groups.google.com/a/google.com/d/msg/chromeos-chatty-firmware/ZSg423wsFPg/26UbdGwjFQAJ
[`fp_updater.sh`]: http://go/cros-fp-updater-nocturne-source
[`bio_fw_updater`]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/biod/tools
[`flashrom`]: https://chromium.googlesource.com/chromiumos/third_party/flashrom/
[STM32F412]: https://www.st.com/resource/en/reference_manual/dm00180369.pdf
[STM32H743]: https://www.st.com/resource/en/reference_manual/dm00314099.pdf
[`board/nocturne_fp/dev_key.pem`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/board/nocturne_fp/dev_key.pem
[`timberslide`]: https://chromium.googlesource.com/chromiumos/platform2/+/master/timberslide
[cros_ec_debugfs]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/9db44685934a2e4bc9180ea2de87a6c429672395/drivers/platform/chrome/cros_ec_debugfs.c
[Fingerprint Factory Requirements]: ./fingerprint-factory-requirements.md
[Chromium OS test image]: https://chromium.googlesource.com/chromiumos/platform/factory/+/master/README.md#building-test-image
[Chrome OS Config]: https://chromium.googlesource.com/chromiumos/platform2/+/master/chromeos-config/README.md
[Chrome OS Config fingerprint]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/chromeos-config/README.md#fingerprint
[section on testing properties]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/chromeos-config/README.md#adding-and-testing-new-properties
[Chrome OS board]: https://chromium.googlesource.com/chromiumos/docs/+/master/developer_guide.md#Select-a-board
[biod chromium-os]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/virtual/target-chromium-os/target-chromium-os-1.ebuild#154
[hatch make.defaults]: https://chromium.googlesource.com/chromiumos/overlays/board-overlays/+/2f075f0e7ce09d3eb460f3c529da463a6201276c/overlay-hatch/profiles/base/make.defaults#22
[Hatch baseboard `make.defaults`]: https://chrome-internal.googlesource.com/chromeos/overlays/baseboard-hatch-private/+/refs/heads/master/profiles/base/make.defaults#17
[hatch model.yaml]: https://chrome-internal.googlesource.com/chromeos/overlays/overlay-hatch-private/+/master/chromeos-base/chromeos-config-bsp-hatch-private/files/model.yaml
[`ec_extras` attribute]: https://chromium.googlesource.com/chromiumos/platform2/+/refs/heads/master/chromeos-config/README.md#build_targets
[FPMCU_FIRMWARE make.defaults]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/profiles/base/make.defaults#157
[`USE` flag]: https://devmanual.gentoo.org/general-concepts/use-flags/index.html
[`USE` flags]: https://devmanual.gentoo.org/general-concepts/use-flags/index.html
[biod release firmware]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/4ea72b588af3394cb9fd1c330dcf726472183dfd/chromeos-base/biod/biod-9999.ebuild#49
[`chromeos-firmware-fpmcu` ebuild]: https://chrome-internal.googlesource.com/chromeos/overlays/chromeos-overlay/+/refs/heads/master/chromeos-base/chromeos-firmware-fpmcu/chromeos-firmware-fpmcu-9999.ebuild
[firmware ebuild]: https://chrome-internal.googlesource.com/chromeos/overlays/chromeos-overlay/+/refs/heads/master/chromeos-base/chromeos-firmware-fpmcu/chromeos-firmware-fpmcu-9999.ebuild#40
[`chromeos-fpmcu-release*` ebuilds]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/master/sys-firmware
[internal manifest]: https://chromium.googlesource.com/chromiumos/docs/+/master/developer_guide.md#get-the-source-code
[Unit Tests]: ../unit_tests.md
[run the unit tests]: ../unit_tests.md#running
[Measuring Power]: ./fingerprint-dev-for-partners.md#measure-power
[dragonclaw]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
