# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from array import array
import ctypes

from Antmicro.Renode.Peripherals.CPU import RegisterValue


def register_bootrom_hook(addr, func):
    self.Machine["sysbus.cpu"].AddHook(addr, func)
    # Fill the bootrom's function pointer entry with the address that the hook is registered to.
    # For simplicity hooks are added on function pointer locations, no the actual function addresses.
    self.Machine.SystemBus.WriteDoubleWord(addr, addr)
    self.Machine.InfoLog("Registering bootrom function at 0x{0:X}", addr)


# Based on: https://chromium.googlesource.com/chromiumos/platform/ec/+/6898a6542ed0238cc182948f56e3811534db1a38/chip/npcx/header.c#43
def register_bootloader():
    class FirmwareHeader(ctypes.LittleEndianStructure):
        _pack_ = 1
        _fields_ = [
            ("anchor", ctypes.c_uint32),
            ("ext_anchor", ctypes.c_uint16),
            ("spi_max_freq", ctypes.c_uint8),
            ("spi_read_mode", ctypes.c_uint8),
            ("cfg_err_detect", ctypes.c_uint8),
            ("fw_load_addr", ctypes.c_uint32),
            ("fw_entry", ctypes.c_uint32),
            ("err_detect_start_addr", ctypes.c_uint32),
            ("err_detect_end_addr", ctypes.c_uint32),
            ("fw_length", ctypes.c_uint32),
            ("flash_size", ctypes.c_uint8),
            ("reserved", ctypes.c_uint8 * 26),
            ("sig_header", ctypes.c_uint32),
            ("sig_fw_image", ctypes.c_uint32),
        ]

    HEADER_SIZE = ctypes.sizeof(FirmwareHeader)
    flash = self.Machine["sysbus.internal_flash"]

    def bootloader(cpu, addr):
        header_data = flash.ReadBytes(0x0, HEADER_SIZE)
        header = FirmwareHeader.from_buffer(array("B", header_data))

        firmware = flash.ReadBytes(HEADER_SIZE, header.fw_length)
        self.Machine.SystemBus.WriteBytes(firmware, header.fw_load_addr)

        cpu.PC = RegisterValue.Create(header.fw_entry, 32)

        self.Machine.InfoLog(
            "Firmware loaded at: 0x{0:X} ({1} bytes). PC = 0x{2:X}",
            header.fw_load_addr,
            header.fw_length,
            header.fw_entry,
        )

    register_bootrom_hook(0x0, bootloader)


# Based on: https://chromium.googlesource.com/chromiumos/platform/ec/+/6898a6542ed0238cc182948f56e3811534db1a38/chip/npcx/trng.c
def register_trng_functions():
    DRGB_BASE_ADDRESS = 0x00000110
    SHA_BASE_ADDRESS = 0x0000015C

    POINTER_SIZE = 0x4

    DRBG_CONTEXT_SIZE = 240

    NCL_STATUS_OK = 0xA5A5

    def create_hook(name, return_value=NCL_STATUS_OK):
        def hook(cpu, addr):
            cpu.NoisyLog(
                "Entering '{0}' hook that returns 0x{1:X}", name, return_value
            )
            cpu.SetRegisterUnsafe(0, RegisterValue.Create(return_value, 32))
            cpu.PC = cpu.LR

        return hook

    DRGB_FUNCTIONS = [
        create_hook("get_context_size", DRBG_CONTEXT_SIZE),
        create_hook("init_context"),
        create_hook("power"),
        create_hook("finalize_context"),
        create_hook("init"),
        create_hook("config"),
        create_hook("instantiate"),
        create_hook("uninstantiate"),
        create_hook("reseed"),
        create_hook("generate"),
        create_hook("clear"),
    ]

    SHA_FUNCTIONS = [
        create_hook("ncl_sha"),
    ]

    for base, collection in [
        (DRGB_BASE_ADDRESS, DRGB_FUNCTIONS),
        (SHA_BASE_ADDRESS, SHA_FUNCTIONS),
    ]:
        for i, func in enumerate(collection):
            register_bootrom_hook(base + i * POINTER_SIZE, func)


def mc_register_bootrom_functions():
    register_bootloader()
    register_trng_functions()
