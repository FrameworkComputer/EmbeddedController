# Library Compiler and Flags

This document contains the compiler version and flags used to build a static
library for use with fingerprint. The flags and versions correspond to the
version of LLVM and flags in use on `Sept 19, 2023`.

## Toolchain

Fingerprint currently uses clang version `17.0.0`.

## Compiler Flags

The following flags come from the log output from the following build commands:

```bash
# From the following build command on Sep 19, 2023:
make BOARD=bloonchipper V=1 build/bloonchipper/RW/core/cortex-m/mpu.o
make BOARD=dartmonkey V=1 build/dartmonkey/RW/core/cortex-m/mpu.o
```

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

If we compile with `CONFIG_LTO` enabled. the following flag is added to compiler and linker
[here](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/core/cortex-m/build.mk;l=39-42;drc=cc1e6b32d29f3061bf5e8a2b8954d6ef1aaaecff):

```bash
# https://crrev.com/c/271291
-flto
```

## Example C Compilation Line

```bash
make BOARD=bloonchipper V=1 build/bloonchipper/RW/core/cortex-m/mpu.o
# Output (with private paths stripped):
/usr/bin/ccache armv7m-cros-eabi-clang -std=gnu11 -Wstrict-prototypes -Wno-pointer-sign -Werror-implicit-function-declaration -Wno-ignored-attributes -DOUTDIR=build/bloonchipper/RW -DCHIP=stm32 -DBOARD_TASKFILE=ec.tasklist -DBOARD=bloonchipper -DCORE=cortex-m -DPROJECT=ec -DCHIP_VARIANT=stm32f412 -DCHIP_FAMILY=STM32F4 -DBOARD_BLOONCHIPPER= -DCHIP_STM32= -DCORE_CORTEX_M= -DCHIP_VARIANT_STM32F412= -DCHIP_FAMILY_STM32F4= -DFINAL_OUTDIR=build/bloonchipper -DPROTOBUF_MIN_PROTOC_VERSION=0  -Iinclude  -Icore/cortex-m/include  -Iinclude/driver  -Icore/cortex-m  -Ichip/stm32  -Iboard/bloonchipper  -Iboard/bloonchipper  -Icommon  -Ifuzz  -Ipower  -Itest  -Icts/common  -Icts/  -Ibuild/bloonchipper/gen  -Iprivate  -Icommon  -Icommon/fpsensor  -Icommon/vboot  -Icommon/spi  -Icommon/spi/flash_reg  -Icommon/spi/flash_reg/private  -Icommon/spi/flash_reg/src  -Icommon/spi/flash_reg/public  -Icommon/usbc  -Icommon/mock  -Idriver  -Idriver/ioexpander  -Idriver/retimer  -Idriver/temp_sensor  -Idriver/sha256  -Idriver/cec  -Idriver/bc12  -Idriver/nfc  -Idriver/led  -Idriver/usb_mux  -Idriver/fingerprint  -Idriver/fingerprint/elan  -Idriver/fingerprint/fpc  -Idriver/fingerprint/fpc/libfp  -Idriver/fingerprint/fpc/bep  -Idriver/ppc  -Idriver/charger  -Idriver/battery  -Idriver/tcpm  -Idriver/wpc  -Ilibc  -Ithird_party/boringssl/common  -Icrypto  -Ibuild/bloonchipper  -Ifuzz  -Itest  -Ithird_party  -Ispi/flash_regpublic   -I.    -DTEST_ec= -DTEST_EC=        -Iinclude/driver  -DSECTION_IS_RW= -DSECTION=RW -DHAS_TASK_FPSENSOR= -DHAS_TASK_RWSIG_RO= -fno-PIC -DCHROMIUM_EC= -DHAVE_PRIVATE -DHAS_TASK_HOOKS= -DHAS_TASK_HOSTCMD= -DHAS_TASK_CONSOLE=  -I../../third_party/boringssl -I../../third_party/boringssl/include -I/mnt/host/source/src/platform/ec/third_party/boringssl/include -D__TRUSTY__ -mcpu=cortex-m4 -mcpu=cortex-m4 -mthumb -Oz      -Wl,-mllvm -Wl,-inline-threshold=-10 -mno-unaligned-access -mfloat-abi=hard -g  -ftrapv -Wall -Wundef -Wno-trigraphs -Wno-format-security -Wno-address-of-packed-member -fno-common -fno-strict-aliasing -fno-strict-overflow -Wimplicit-fallthrough -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Werror -Werror=uninitialized -Wno-unused-function  -ffunction-sections -fno-delete-null-pointer-checks -fno-PIC -MMD -MP -MF build/bloonchipper/RW/core/cortex-m/mpu.o.d -c core/cortex-m/mpu.c -MT build/bloonchipper/RW/core/cortex-m/mpu.o -o build/bloonchipper/RW/core/cortex-m/mpu.o
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
