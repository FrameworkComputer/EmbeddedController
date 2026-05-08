# RTK Flame

The code in this directory builds the Realtek flash upload monitor binary, named
`rts5915_flash_upload.bin`. This binary executes from RAM and provides routines
for erasing, programming, and verifying the internal SPI flash on Realtek ECs.

> **Note** - If you make changes to this project, you must uprev the
> binary file used in the servo dockerfile. See instructions below.

## Updating the servo dockerfile

Whenever you make changes to the `rtk_flame` project, you must manually update the
version installed into the servo dockerfile.

1. Build and test your changes.
1. Upload a CL with your changes for review.
1. Add the current date to the new filename. For example
  `rts5915_flash_upload-2026.05.08.bin`.
1. Upload the renamed file to Google Cloud Storage (GCS).
    * Use the `gcloud storage` command to upload the file.
    * Note that the name of the file must be unique. The `-n` option prevents
      overwriting an existing file.
    * The `-a publicRead` option makes the file publicly readable.
```bash
    gcloud storage cp -n -a publicRead rts5915_flash_upload-2026.05.08.bin \
        gs://chromeos-localmirror/distfiles/cros_ec/rtk_flame/
```

1. Edit the binary installed by the [servo dockerfile]
    * Find the instruction that fetches the Realtek upload binary from GCS.
    * Modify the `wget` instruction to point to the new file uploaded
      above.
    * Upload a CL to the `third_party/hdctools` repository with your dockerfile
      change.

## Build instructions

### Building standalone

You can build the upload binary using the `rtk_flame` zmake project.

```bash
zmake build rtk_flame
```

The binary is located in the build output directory.

```bash
./build/zephyr/rtk_flame/build-singleimage/rts5915_flash_upload.bin
```

## UART Update Workflow

Realtek ECs can be reprogrammed using a UART interface via the `rtkupdate` host
utility. This workflow is implemented in the [`flash_ec`] script (refer to the
`flash_rtk_uart` function).

The update process involves several steps to ensure the EC is in the correct
state, write protection is disabled, and the programming routines are loaded
into RAM before flashing.

1. **Prepare the EC for UART update**. This typically involves asserting reset, enabling the UART update strap pins (boot0), and releasing reset.
    *(When using servod, this is handled via controls like `gsc_ec_reset:on` and `ccd_ec_boot_mode_uut:on`)*.
1. **Disable Write Protection**. Before loading the monitor or flashing, ensure write protection is disabled.
```bash
    rtkupdate --method=wp --protect=0 --uart_device=<tty-dev>
```
1. **Load the Flame Monitor Binary**. Use the `frame` method to load the upload routines into EC RAM.
```bash
    rtkupdate --method=frame --uart_device=<tty-dev> --file=build/zephyr/rtk_flame/build-singleimage/rts5915_flash_upload.bin
```
1. **Program the Firmware Image**. Once the frame routines are active, flash the actual firmware binary.
```bash
    rtkupdate --method=flash --spi_start=0 --uart_device=<tty-dev> --file=<fw.bin>
```
1. **(Optional) Verify the Flashed Image**. You can read back the image to verify it.
```bash
    rtkupdate --method=read_bin --spi_start=0 --uart_device=<tty-dev> --file=<read_back.bin> --bin_length=<size>
```

[`flash_ec`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/flash_ec
[servo dockerfile]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/hdctools/dockerfiles/Dockerfile.base
