# Library Compiler and Flags

This document contains the compiler version and flags used to build a static
library for use with fingerprint. The flags and versions correspond to the
version of LLVM and flags in use on `Sept 19, 2023`.

## Toolchain

Fingerprint currently uses clang version `17.0.0` and newlib version `4.1.0`.

Please use toolchain available within the ChromiumOS SDK chroot. See the
[ChromiumOS Developer Guide](https://chromium.googlesource.com/chromiumos/docs/+/main/developer_guide.md#Building-ChromiumOS)
for instructions on how to set it up.

-   Prerequisites (most important is depot_tools)
-   Get the Source
-   Create a chroot
-   Run `cros_sdk` from within `~/chromiumos`. The compiler
    `armv7m-cros-eabi-clang` is available within this chroot.

## Compiler Flags

### Bloonchipper (libbep)

```bash
armv7m-cros-eabi-clang -mcpu=cortex-m4
```

### Dartmonkey (libfp)

```bash
armv7m-cros-eabi-clang -mcpu=cortex-m7
```

### Common Flags

```bash
-mthumb
-mfloat-abi=hard
-mno-unaligned-access
-std=gnu11
# Reduce code size.
# https://crrev.com/c/3199737
-Oz
-g

# Decrease code size by reducing inlining.
# https://crrev.com/c/4545442
-Wl,-mllvm -Wl,-inline-threshold=-10

# Add ability to remove unused code at link time.
# https://stackoverflow.com/q/6687630
# https://crrev.com/c/189224
-ffunction-sections
# Generate traps for signed overflow on addition, subtraction, multiplication
# operations.
# https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html#index-ftrapv
# https://crrev.com/c/4007158
-ftrapv
-fno-PIC
-fno-asynchronous-unwind-tables
-fno-common
-fno-delete-null-pointer-checks
-fno-exceptions
-fno-strict-aliasing
-fno-strict-overflow
-fno-unwind-tables

-Wall
-Werror
-Werror-implicit-function-declaration
-Werror=uninitialized
-Wimplicit-fallthrough
-Wno-address-of-packed-member
-Wno-format-security
-Wno-ignored-attributes
-Wno-pointer-sign
-Wno-trigraphs
-Wno-unused-function
-Wstrict-prototypes
-Wundef
```

### LTO Enable Flags

If we compile with `CONFIG_LTO` enabled. the following flag is added to compiler
and linker
[here](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/core/cortex-m/build.mk;l=39-42;drc=cc1e6b32d29f3061bf5e8a2b8954d6ef1aaaecff):

```bash
# https://crrev.com/c/271291
-flto
```

## Example C Compilation Line

```bash
# The above arguments are from the following build command on Sep 19, 2023:
make BOARD=dartmonkey V=1 build/dartmonkey/RW/core/cortex-m/mpu.o
# Output removed, since it is similar to bloonchipper.
make BOARD=bloonchipper V=1 build/bloonchipper/RW/core/cortex-m/mpu.o
# Output (with private paths stripped):
/usr/bin/ccache armv7m-cros-eabi-clang -std=gnu11 -Wstrict-prototypes -Wno-pointer-sign -Werror-implicit-function-declaration -Wno-ignored-attributes -DOUTDIR=build/bloonchipper/RW -DCHIP=stm32 -DBOARD_TASKFILE=ec.tasklist -DBOARD=bloonchipper -DCORE=cortex-m -DPROJECT=ec -DCHIP_VARIANT=stm32f412 -DCHIP_FAMILY=STM32F4 -DBOARD_BLOONCHIPPER= -DCHIP_STM32= -DCORE_CORTEX_M= -DCHIP_VARIANT_STM32F412= -DCHIP_FAMILY_STM32F4= -DFINAL_OUTDIR=build/bloonchipper -DPROTOBUF_MIN_PROTOC_VERSION=0  -Iinclude  -Icore/cortex-m/include  -Iinclude/driver  -Icore/cortex-m  -Ichip/stm32  -Iboard/bloonchipper  -Iboard/bloonchipper  -Icommon  -Ifuzz  -Ipower  -Itest  -Icts/common  -Icts/  -Ibuild/bloonchipper/gen  -Iprivate  -Icommon  -Icommon/fpsensor  -Icommon/vboot  -Icommon/spi  -Icommon/spi/flash_reg  -Icommon/spi/flash_reg/private  -Icommon/spi/flash_reg/src  -Icommon/spi/flash_reg/public  -Icommon/usbc  -Icommon/mock  -Idriver  -Idriver/ioexpander  -Idriver/retimer  -Idriver/temp_sensor  -Idriver/sha256  -Idriver/cec  -Idriver/bc12  -Idriver/nfc  -Idriver/led  -Idriver/usb_mux  -Idriver/fingerprint  -Idriver/fingerprint/elan  -Idriver/fingerprint/fpc  -Idriver/fingerprint/fpc/libfp  -Idriver/fingerprint/fpc/bep  -Idriver/ppc  -Idriver/charger  -Idriver/battery  -Idriver/tcpm  -Idriver/wpc  -Ilibc  -Ithird_party/boringssl/common  -Icrypto  -Ibuild/bloonchipper  -Ifuzz  -Itest  -Ithird_party  -Ispi/flash_regpublic   -I.    -DTEST_ec= -DTEST_EC=        -Iinclude/driver  -DSECTION_IS_RW= -DSECTION=RW -DHAS_TASK_FPSENSOR= -DHAS_TASK_RWSIG_RO= -fno-PIC -DCHROMIUM_EC= -DHAVE_PRIVATE -DHAS_TASK_HOOKS= -DHAS_TASK_HOSTCMD= -DHAS_TASK_CONSOLE=  -I../../third_party/boringssl -I../../third_party/boringssl/include -I/mnt/host/source/src/platform/ec/third_party/boringssl/include -D__TRUSTY__ -mcpu=cortex-m4 -mcpu=cortex-m4 -mthumb -Oz      -Wl,-mllvm -Wl,-inline-threshold=-10 -mno-unaligned-access -mfloat-abi=hard -g  -ftrapv -Wall -Wundef -Wno-trigraphs -Wno-format-security -Wno-address-of-packed-member -fno-common -fno-strict-aliasing -fno-strict-overflow -Wimplicit-fallthrough -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Werror -Werror=uninitialized -Wno-unused-function  -ffunction-sections -fno-delete-null-pointer-checks -fno-PIC -MMD -MP -MF build/bloonchipper/RW/core/cortex-m/mpu.o.d -c core/cortex-m/mpu.c -MT build/bloonchipper/RW/core/cortex-m/mpu.o -o build/bloonchipper/RW/core/cortex-m/mpu.o
```

The linking step to create `RO/ec.RO.elf` and `RW/ec.RW.elf`:

```bash
# Output (with private paths stripped):
armv7m-cros-eabi-clang++ build/bloonchipper/RO/board/bloonchipper/board.o build/bloonchipper/RO/board/bloonchipper/fpsensor_detect.o build/bloonchipper/RO/chip/stm32/bkpdata.o build/bloonchipper/RO/chip/stm32/clock-f.o build/bloonchipper/RO/chip/stm32/clock-stm32f4.o build/bloonchipper/RO/chip/stm32/dma-stm32f4.o build/bloonchipper/RO/chip/stm32/flash-f.o build/bloonchipper/RO/chip/stm32/flash-stm32f4.o build/bloonchipper/RO/chip/stm32/fpu.o build/bloonchipper/RO/chip/stm32/gpio-stm32f4.o build/bloonchipper/RO/chip/stm32/gpio.o build/bloonchipper/RO/chip/stm32/host_command_common.o build/bloonchipper/RO/chip/stm32/hwtimer32.o build/bloonchipper/RO/chip/stm32/otp-stm32f4.o build/bloonchipper/RO/chip/stm32/spi.o build/bloonchipper/RO/chip/stm32/spi_controller.o build/bloonchipper/RO/chip/stm32/system.o build/bloonchipper/RO/chip/stm32/trng.o build/bloonchipper/RO/chip/stm32/uart.o build/bloonchipper/RO/chip/stm32/usart-stm32f4.o build/bloonchipper/RO/chip/stm32/usart.o build/bloonchipper/RO/chip/stm32/usart_host_command.o build/bloonchipper/RO/chip/stm32/usart_rx_dma.o build/bloonchipper/RO/chip/stm32/usart_rx_interrupt-stm32f4.o build/bloonchipper/RO/chip/stm32/usart_tx_dma.o build/bloonchipper/RO/chip/stm32/usart_tx_interrupt.o build/bloonchipper/RO/chip/stm32/watchdog.o build/bloonchipper/RO/common/console.o build/bloonchipper/RO/common/console_output.o build/bloonchipper/RO/common/ec_features.o build/bloonchipper/RO/common/extpower_common.o build/bloonchipper/RO/common/flash.o build/bloonchipper/RO/common/fmap.o build/bloonchipper/RO/common/fpsensor/fpsensor_detect_strings.o build/bloonchipper/RO/common/gettimeofday.o build/bloonchipper/RO/common/gpio.o build/bloonchipper/RO/common/gpio_commands.o build/bloonchipper/RO/common/hooks.o build/bloonchipper/RO/common/host_command.o build/bloonchipper/RO/common/host_command_task.o build/bloonchipper/RO/common/host_event_commands.o build/bloonchipper/RO/common/irq_locking.o build/bloonchipper/RO/common/main.o build/bloonchipper/RO/common/memory_commands.o build/bloonchipper/RO/common/mkbp_event.o build/bloonchipper/RO/common/mkbp_fifo.o build/bloonchipper/RO/common/panic_output.o build/bloonchipper/RO/common/peripheral.o build/bloonchipper/RO/common/printf.o build/bloonchipper/RO/common/queue.o build/bloonchipper/RO/common/queue_policies.o build/bloonchipper/RO/common/rollback.o build/bloonchipper/RO/common/rsa.o build/bloonchipper/RO/common/rwsig.o build/bloonchipper/RO/common/sha256.o build/bloonchipper/RO/common/shared_mem_libc.o build/bloonchipper/RO/common/system.o build/bloonchipper/RO/common/system_boot_time.o build/bloonchipper/RO/common/timer.o build/bloonchipper/RO/common/trng.o build/bloonchipper/RO/common/uart_buffering.o build/bloonchipper/RO/common/uart_hostcmd.o build/bloonchipper/RO/common/uart_printf.o build/bloonchipper/RO/common/uptime.o build/bloonchipper/RO/common/usb_pd_flags.o build/bloonchipper/RO/common/util.o build/bloonchipper/RO/common/vboot/common.o build/bloonchipper/RO/common/vboot/vb21_lib.o build/bloonchipper/RO/common/version.o build/bloonchipper/RO/core/cortex-m/cpu.o build/bloonchipper/RO/core/cortex-m/debug.o build/bloonchipper/RO/core/cortex-m/fpu.o build/bloonchipper/RO/core/cortex-m/init.o build/bloonchipper/RO/core/cortex-m/mpu.o build/bloonchipper/RO/core/cortex-m/panic.o build/bloonchipper/RO/core/cortex-m/switch.o build/bloonchipper/RO/core/cortex-m/task.o build/bloonchipper/RO/core/cortex-m/vecttable.o build/bloonchipper/RO/core/cortex-m/watchdog.o build/bloonchipper/RO/crypto/elliptic_curve_key.o build/bloonchipper/RO/libc/syscalls.o build/bloonchipper/RO/third_party/boringssl/common/sysrand.o  -g -no-pie -Wl,-X -Wl,--gc-sections -Wl,--build-id=none -lclang_rt.builtins-armv7m -Lbuild/bloonchipper/third_party/boringssl/crypto -lcrypto -mcpu=cortex-m4 -mcpu=cortex-m4 -mthumb -Oz        -Wl,-mllvm -Wl,-inline-threshold=-10 -mno-unaligned-access -mfloat-abi=hard -lnosys -o build/bloonchipper/RO/ec.RO.elf -Wl,-T,build/bloonchipper/RO/ec.RO.lds -Wl,-Map,build/bloonchipper/RO/ec.RO.map
```

```bash
# Output (with private paths stripped):
armv7m-cros-eabi-clang++ build/bloonchipper/RW/board/bloonchipper/board.o build/bloonchipper/RW/board/bloonchipper/board_rw.o build/bloonchipper/RW/board/bloonchipper/fpsensor_detect.o build/bloonchipper/RW/board/bloonchipper/fpsensor_detect_rw.o build/bloonchipper/RW/chip/stm32/bkpdata.o build/bloonchipper/RW/chip/stm32/clock-f.o build/bloonchipper/RW/chip/stm32/clock-stm32f4.o build/bloonchipper/RW/chip/stm32/dma-stm32f4.o build/bloonchipper/RW/chip/stm32/flash-f.o build/bloonchipper/RW/chip/stm32/flash-stm32f4.o build/bloonchipper/RW/chip/stm32/fpu.o build/bloonchipper/RW/chip/stm32/gpio-stm32f4.o build/bloonchipper/RW/chip/stm32/gpio.o build/bloonchipper/RW/chip/stm32/host_command_common.o build/bloonchipper/RW/chip/stm32/hwtimer32.o build/bloonchipper/RW/chip/stm32/otp-stm32f4.o build/bloonchipper/RW/chip/stm32/spi.o build/bloonchipper/RW/chip/stm32/spi_controller.o build/bloonchipper/RW/chip/stm32/system.o build/bloonchipper/RW/chip/stm32/trng.o build/bloonchipper/RW/chip/stm32/uart.o build/bloonchipper/RW/chip/stm32/usart-stm32f4.o build/bloonchipper/RW/chip/stm32/usart.o build/bloonchipper/RW/chip/stm32/usart_host_command.o build/bloonchipper/RW/chip/stm32/usart_rx_dma.o build/bloonchipper/RW/chip/stm32/usart_rx_interrupt-stm32f4.o build/bloonchipper/RW/chip/stm32/usart_tx_dma.o build/bloonchipper/RW/chip/stm32/usart_tx_interrupt.o build/bloonchipper/RW/chip/stm32/watchdog.o build/bloonchipper/RW/common/console.o build/bloonchipper/RW/common/console_output.o build/bloonchipper/RW/common/ec_features.o build/bloonchipper/RW/common/extpower_common.o build/bloonchipper/RW/common/flash.o build/bloonchipper/RW/common/fmap.o build/bloonchipper/RW/common/fpsensor/fpsensor.o build/bloonchipper/RW/common/fpsensor/fpsensor_auth_commands.o build/bloonchipper/RW/common/fpsensor/fpsensor_auth_crypto_stateful.o build/bloonchipper/RW/common/fpsensor/fpsensor_auth_crypto_stateless.o build/bloonchipper/RW/common/fpsensor/fpsensor_crypto.o build/bloonchipper/RW/common/fpsensor/fpsensor_debug.o build/bloonchipper/RW/common/fpsensor/fpsensor_detect_strings.o build/bloonchipper/RW/common/fpsensor/fpsensor_state.o build/bloonchipper/RW/common/fpsensor/fpsensor_state_without_driver_info.o build/bloonchipper/RW/common/gettimeofday.o build/bloonchipper/RW/common/gpio.o build/bloonchipper/RW/common/gpio_commands.o build/bloonchipper/RW/common/hooks.o build/bloonchipper/RW/common/host_command.o build/bloonchipper/RW/common/host_command_task.o build/bloonchipper/RW/common/host_event_commands.o build/bloonchipper/RW/common/irq_locking.o build/bloonchipper/RW/common/main.o build/bloonchipper/RW/common/memory_commands.o build/bloonchipper/RW/common/mkbp_event.o build/bloonchipper/RW/common/mkbp_fifo.o build/bloonchipper/RW/common/panic_output.o build/bloonchipper/RW/common/peripheral.o build/bloonchipper/RW/common/printf.o build/bloonchipper/RW/common/queue.o build/bloonchipper/RW/common/queue_policies.o build/bloonchipper/RW/common/rollback.o build/bloonchipper/RW/common/sha256.o build/bloonchipper/RW/common/shared_mem_libc.o build/bloonchipper/RW/common/spi_commands.o build/bloonchipper/RW/common/system.o build/bloonchipper/RW/common/system_boot_time.o build/bloonchipper/RW/common/timer.o build/bloonchipper/RW/common/trng.o build/bloonchipper/RW/common/uart_buffering.o build/bloonchipper/RW/common/uart_hostcmd.o build/bloonchipper/RW/common/uart_printf.o build/bloonchipper/RW/common/uptime.o build/bloonchipper/RW/common/usb_pd_flags.o build/bloonchipper/RW/common/util.o build/bloonchipper/RW/common/vboot/vb21_lib.o build/bloonchipper/RW/common/version.o build/bloonchipper/RW/core/cortex-m/cpu.o build/bloonchipper/RW/core/cortex-m/debug.o build/bloonchipper/RW/core/cortex-m/fpu.o build/bloonchipper/RW/core/cortex-m/init.o build/bloonchipper/RW/core/cortex-m/mpu.o build/bloonchipper/RW/core/cortex-m/panic.o build/bloonchipper/RW/core/cortex-m/switch.o build/bloonchipper/RW/core/cortex-m/task.o build/bloonchipper/RW/core/cortex-m/vecttable.o build/bloonchipper/RW/core/cortex-m/watchdog.o build/bloonchipper/RW/crypto/elliptic_curve_key.o build/bloonchipper/RW/driver/fingerprint/fpc/bep/fpc_misc.o build/bloonchipper/RW/driver/fingerprint/fpc/bep/fpc_private.o build/bloonchipper/RW/driver/fingerprint/fpc/bep/fpc_sensor_spi.o build/bloonchipper/RW/driver/fingerprint/fpc/bep/fpc_timebase.o build/bloonchipper/RW/driver/fingerprint/fpc/fpc_sensor.o build/bloonchipper/RW/libc/syscalls.o build/bloonchipper/RW/third_party/boringssl/common/sysrand.o  -g -no-pie -Wl,-X -Wl,--gc-sections -Wl,--build-id=none -lclang_rt.builtins-armv7m -Lbuild/bloonchipper/libsharedobjs/private -lfpbep -Lbuild/bloonchipper/third_party/boringssl/crypto -lcrypto -mcpu=cortex-m4 -mcpu=cortex-m4 -mthumb -Oz      -Wl,-mllvm -Wl,-inline-threshold=-10 -mno-unaligned-access -mfloat-abi=hard -lnosys -o build/bloonchipper/RW/ec.RW.elf -Wl,-T,build/bloonchipper/RW/ec.RW.lds -Wl,-Map,build/bloonchipper/RW/ec.RW.map
```

Final linking to combine `RO/ec.RO.elf` and `RW/ec.RW.elf` into `ec.obj`:

```bash
# Output (with private paths stripped):
/usr/bin/ccache armv7m-cros-eabi-clang -Wl,-T build/bloonchipper/firmware_image.lds -nostdlib -DOUTDIR=build/bloonchipper/ -DCHIP=stm32 -DBOARD_TASKFILE=ec.tasklist -DBOARD=bloonchipper -DCORE=cortex-m -DPROJECT=ec -DCHIP_VARIANT=stm32f412 -DCHIP_FAMILY=STM32F4 -DBOARD_BLOONCHIPPER= -DCHIP_STM32= -DCORE_CORTEX_M= -DCHIP_VARIANT_STM32F412= -DCHIP_FAMILY_STM32F4= -DFINAL_OUTDIR=build/bloonchipper -DPROTOBUF_MIN_PROTOC_VERSION=0  -Iinclude  -Icore/cortex-m/include  -Iinclude/driver  -Icore/cortex-m  -Ichip/stm32  -Iboard/bloonchipper  -Iboard/bloonchipper  -Icommon  -Ifuzz  -Ipower  -Itest  -Icts/common  -Icts/  -Ibuild/bloonchipper/gen  -Iprivate  -Icommon  -Icommon/fpsensor  -Icommon/vboot  -Icommon/spi  -Icommon/spi/flash_reg  -Icommon/spi/flash_reg/private  -Icommon/spi/flash_reg/src  -Icommon/spi/flash_reg/public  -Icommon/usbc  -Icommon/mock  -Idriver  -Idriver/ioexpander  -Idriver/retimer  -Idriver/temp_sensor  -Idriver/sha256  -Idriver/cec  -Idriver/bc12  -Idriver/nfc  -Idriver/led  -Idriver/usb_mux  -Idriver/fingerprint  -Idriver/fingerprint/elan  -Idriver/fingerprint/fpc  -Idriver/fingerprint/fpc/libfp  -Idriver/fingerprint/fpc/bep  -Idriver/ppc  -Idriver/charger  -Idriver/battery  -Idriver/tcpm  -Idriver/wpc  -Ilibc  -Ithird_party/boringssl/common  -Icrypto  -Ibuild/bloonchipper  -Ifuzz  -Itest  -Ithird_party  -Ispi/flash_regpublic  -I.    -DTEST_ec= -DTEST_EC=        -Iinclude/driver  -DSECTION_IS_= -DSECTION=  -fno-PIC -DCHROMIUM_EC= -DHAVE_PRIVATE -DHAS_TASK_HOOKS= -DHAS_TASK_HOSTCMD= -DHAS_TASK_CONSOLE=  -I../../third_party/boringssl -I../../third_party/boringssl/include -I/mnt/host/source/src/platform/ec/third_party/boringssl/include -D__TRUSTY__ -mcpu=cortex-m4 -mcpu=cortex-m4 -mthumb -Oz      -Wl,-mllvm -Wl,-inline-threshold=-10 -mno-unaligned-access -mfloat-abi=hard -g  -ftrapv -Wall -Wundef -Wno-trigraphs -Wno-format-security -Wno-address-of-packed-member -fno-common -fno-strict-aliasing -fno-strict-overflow -Wimplicit-fallthrough -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Werror -Werror=uninitialized -Wno-unused-function  -ffunction-sections -fno-delete-null-pointer-checks -fno-PIC -Wl,--build-id=none -o build/bloonchipper/ec.obj common/firmware_image.S
```

## Exact Clang/LLVM Version

```bash
armv7m-cros-eabi-clang --version
# Output:
Chromium OS 17.0_pre496208_p20230501-r26 clang version 17.0.0 (/mnt/host/source/src/third_party/llvm-project 98f5a340975bc00197c57e39eb4ca26e2da0e8a2)
Target: armv7m-cros-unknown-eabi
Thread model: posix
InstalledDir: /usr/bin
```
