# Firmware Write Protection

[TOC]

This is a somewhat tricky topic since write protection implementations can
differ between chips and the hardware write protection has changed over time,
so please edit or open a bug if something is not clear.

## Terminology

## RO and RW

MCUs running the EC code have read-only (RO) and read-write (RW) firmware.
Coming out of reset, the MCU boots into its RO firmware.

In the case of the EC, the RO firmware boots the host and asks it verify a hash
of the RW firmware (software sync). If the RW firmware is invalid, it is
updated from a copy in the host's RW firmware.

In the case of the FPMCU, the RO firmware uses the public key embedded in it to
validate the signature of the RW firmware. If the RW firmware is invalid it
does not jump to the RW firmware.

Once the RW firmware is validated, the MCU jumps to it (without rebooting). The
RO firmware is locked in the factory and is never changed. The RW firmware can
be updated later by pushing a new system firmware containing an updated RW
region.

Note that both the RO and RW firmware regions are normally protected once write
protect has been turned on.

In the case of the EC, the RW region is unprotected at MCU boot until it has
been verified by the host. The RW region is protected before the Linux kernel
is loaded.

In the case of the FPMCU, the RW region is protected before jumping the RO
firmware jumps to it.

## Hardware Write Protect {#hw_wp}

On modern Chrome OS devices, the Cr50 (aka GSC / TPM) provides a "hardware
write protect" GPIO that is connected to the AP SPI flash, EC SPI flash,
EEPROM, and FPMCU via a [GPIO][write_protect_gpio].  This "hardware write
protect" can only be disabled with servo or suzyq (["CCD open"]) and
corresponds to [`OverrideWP`] in ccd. Disabling this write protect disables it
for everything connected to this signal.

In the case of the FPMCU, the hardware write protect GPIO is tied to the STM32
`BOOT0` pin, which is what tells the MCU to enter the STM32 bootloader mode.

You may see various references to a [write protect screw in
documentation][wp_screw]. Older Chrome OS devices had a write protect screw
that had to be physically removed. More details on this history can be found
here: http://go/cros-wp-status.

Another way of disabling hardware write protection is to remove the battery;
this method is mainly used during bringup.

Additional reference:
https://www.google.com/chromeos/partner/fe/docs/cpfe/firmwaretestmanual.html#hardware-write-protect

## Changing Hardware Write Protection

Modifying the state of hardware write protection (via Cr50 GPIO) can be done
if the ["CCD open"] process has been completed.

*** note
`servod` *must* be running for `dut-control` to work. See the [Servo] page for
details.
***

### Enable Hardware Write Protection

```bash
(chroot)$ dut-control fw_wp_state:force_on
```

### Disable Hardware Write Protection

```bash
(chroot)$ dut-control fw_wp_state:force_off
```

### Enable/Disable Hardware Write Protection via Cr50 Console

You can use the following commands from the [Cr50 console]:

```bash
wp disable
```

```bash
wp enable
```

```bash
wp follow_batt_pres
```

## Software Write Protect

Software-based write protect state stored in non-volatile memory. If hardware
write protect is enabled, software write protect can be enabled but can’t be
disabled. If hardware write protect is disabled, software write protect can be
enabled or disabled (note that some implementations require an EC reset to
disable software write protect).

The underlying mechanism implementing software write protect may differ between
EC chips. However the common requirements are that software write protect can
only be disabled when hardware write protect is off and that the RO firmware
must be protected before jumping to RW firmware if protection is enabled.

Additional reference:
https://www.google.com/chromeos/partner/fe/docs/cpfe/firmwaretestmanual.html#software-write-protect

## Changing Software Write Protection

*** note
*NOTE*: You cannot disable software write protect if hardware write protect is
enabled.
***

Software write protection can be toggled with `ectool --name=cros_fp flashprotect
enable/disable`, which sends the `EC_CMD_FLASH_PROTECT` command toggling
`EC_FLASH_PROTECT_RO_AT_BOOT` (changing `--name` to target different ECs).

### Changing Software Write Protection with `ectool`

#### `ectool flashprotect`

Print out current flash protection state.

```
Flash protect flags: 0x0000000f wp_gpio_asserted ro_at_boot ro_now all_now
Valid flags:         0x0000003f wp_gpio_asserted ro_at_boot ro_now all_now STUCK INCONSISTENT
Writable flags:      0x00000000
```

`Flash protect flags` - Current flags that are set.

`Valid flags` - All the options for flash protection.

`Writable flags` - The flags that currently can be changed. (In this case, no
flags can be changed).

Flags:

*   `wp_gpio_asserted` - Whether the hardware write protect GPIO is currently
    asserted (read only).

*   `ro_at_boot` - Whether the EC will write protect the RO firmware on the next
    boot of the EC.

*   `ro_now` - Protect the read-only portion of flash immediately. Requires
    hardware WP be enabled.

*   `all_now` - Protect the entire flash (including RW) immediately. Requires
    hardware WP be enabled.

*   `STUCK` - Flash protection settings have been fused and can’t be cleared
    (should not happen during normal operation. Read only.)

*   `INCONSISTENT` - One or more banks of flash is not protected when it should
    be (should not happen during normal operation. Read only.).

#### `ectool flashprotect enable`

Set `ro_at_boot` flag. The next time the EC is reset it will protect the flash.
Note that this requires a cold reset.

#### `ectool flashprotect enable now`

Set `ro_at_boot` `ro_now all_now` flags and immediately protect the flash. Note
that this will fail if hardware write protect is disabled.

#### `ectool flashprotect disable`

Clear `ro_at_boot` flag. This can only be cleared if the EC booted without
hardware write protect enabled.

Note that you must reset the EC to clear write protect after removing the screw.
If the `ro_at_boot` flag set and the EC resets with the HW gpio disabled, the EC
will leave the flash unprotected (`ro_now` and `all_now` flags are not set) but
leave `ro_at_boot` flag set.

### Changing Software Write Protection with `flashrom`

#### View the current state of software write protection

```bash
(chroot) $ flashrom -p ec --wp-status
```

```
WP: status: 0x00
WP: status.srp0: 0
WP: write protect is disabled.
WP: write protect range: start=0x00000000, len=0x00000000
```

#### Enable software write protection

This is immediate. The protection range indicates the RO region of the firmware.

```bash
(chroot) $ flashrom -p ec --wp-enable
```

```
SUCCESS
```

```bash
(chroot) $ flashrom -p ec --wp-status
```

```
WP: status: 0x80
WP: status.srp0: 1
WP: write protect is enabled.
WP: write protect range: start=0x00000000, len=0x0001f800
```

#### Disable software write protection

Disable can only be done with hardware write protect disabled.

```bash
(chroot) $ flashrom -p ec --wp-disable
```

```
FAILED: RO_AT_BOOT is not clear.
FAILED
```

Reboot with [hardware write protection](#hw_wp) disabled. Note that protection
is still enabled but the protection range is zero.

```bash
(chroot) $ flashrom -p ec --wp-status
```

```
WP: status: 0x80
WP: status.srp0: 1
WP: write protect is enabled.
WP: write protect range: start=0x00000000, len=0x00000000
```

```bash
(chroot) $ flashrom -p ec --wp-disable
```

```
SUCCESS
```

## `system_is_locked()`

The [`system_is_locked()`] function in the EC code returns false if the HW
write protect GPIO is disabled or the read-only firmware is not protected.

One way this is used in the FPMCU source is to compile test or debug
functionality into the firmware. Guarding the test functionality with
`system_is_locked` allows us to execute the test code in automated testing by
disabling the hardware write protection; this means we can run the automated
tests against the exact same firmware we ship, rather than a different version
that has test functionality compiled in or out.

## RDP1 {#rdp1}

Stands for Readout Protection Level 1.

Protects user flash memory against a debugger (JTAG/SWD) or potential malicious
code stored in RAM by disabling access (a bus error is generated when read
access is requested). Otherwise (no debugger connected and no boot in RAM set),
all read/program/erase operations from/to flash are allowed.

When switching to a lower level of RDP (i.e., setting to 0), the user flash
memory is mass erased (set to all `0xFF`).

Note that this completely destroys *all* of the firmware, including the RO
section.

### Additional References

https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/1222094

## EC Flash Read/Write Command Write Protection Checks

The EC code command handlers (`command_flash_erase`, `command_flash_write`,
etc.) return an error if `EC_FLASH_PROTECT_ALL_NOW` is set.

["CCD open"]: https://chromium.googlesource.com/chromiumos/platform/ec/+/cr50_stab/docs/case_closed_debugging_cr50.md#Open-CCD
[Cr50 console]: https://chromium.googlesource.com/chromiumos/platform/ec/+/cr50_stab/docs/case_closed_debugging_cr50.md#Consoles
[Servo]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/README.md
[`OverrideWP`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/cr50_stab/docs/case_closed_debugging_cr50.md
[`system_is_locked()`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/aaba1d5efd51082d143ce2ac64e6caf9cb14d5e5/common/system.c#195
[wp_screw]: https://www.chromium.org/chromium-os/firmware-porting-guide/firmware-ec-write-protection
[write_protect_gpio]: https://chromium.googlesource.com/chromiumos/platform/ec/+/aaba1d5efd51082d143ce2ac64e6caf9cb14d5e5/include/ec_commands.h#1599
