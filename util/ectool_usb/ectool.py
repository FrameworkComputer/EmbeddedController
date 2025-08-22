# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A USB version of ectool."""

import argparse
import sys
import time

import command
import communication
import ec_commands as commands


def cmd_get_version(_args, comm) -> int:
    """Prints the version of the EC."""
    get_ver = commands.GetVersionCmd1()
    ret = get_ver.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Error getting version: {ret.name}")
        return ret

    print("RO: " + get_ver.response.ro_ver.decode("ascii"))
    print("RW: " + get_ver.response.rw_ver.decode("ascii"))
    print("FWID_RO: " + get_ver.response.fwid_ro.decode("ascii"))
    print(
        "Current image: "
        + commands.ImageType.get_image_name(get_ver.response.curr_image)
    )
    print("FWID_RW: " + get_ver.response.fwid_rw.decode("ascii"))

    return ret


def cmd_fl_info(args, comm) -> int:
    """Prints flash information."""
    flash_info = commands.FlashInfoCmd2(args.num_banks_desc)
    ret = flash_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to retrieve flash info: {ret.name}")
        return ret

    print("Total flash: " + str(flash_info.response.flash_size))
    print("Flags: " + hex(flash_info.response.flags))
    print("Maximum size to write: " + str(flash_info.response.write_ideal_size))
    print(
        "Number of banks present: " + str(flash_info.response.num_banks_total)
    )
    print(
        "Number of banks described: " + str(flash_info.response.num_banks_desc)
    )

    for i, ec_flash_bank in enumerate(flash_info.response.ec_flash_banks):
        print(f"Bank {i}:")
        print("\tNumber of sectors: " + str(ec_flash_bank.count))
        print(
            "\tSize of sector (in power of 2): " + hex(ec_flash_bank.size_exp)
        )
        print(
            "\tMinimal write size (in power of 2): "
            + hex(ec_flash_bank.write_size_exp)
        )
        print(
            "\tErase size (in power of 2): " + hex(ec_flash_bank.erase_size_exp)
        )
        print(
            "\tSize for write protection (in power of 2): "
            + hex(ec_flash_bank.protect_size_exp)
        )
    return ret


def cmd_pr_info(_args, comm) -> int:
    """Prints protocol information."""
    pr_info = commands.ProtocolInfoCmd0()
    ret = pr_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to retrieve protocol info: {ret.name}")
        return ret

    print("Version: " + str(pr_info.response.protocol_versions))
    print(
        "Max request packet: " + str(pr_info.response.max_request_packet_size)
    )
    print(
        "Max response packet: " + str(pr_info.response.max_response_packet_size)
    )
    print("Flags: " + hex(pr_info.response.flags))

    return ret


fp_modes = {
    "deepsleep": 0b1,
    "finger_down": 0b1 << 1,
    "finger_up": 0b1 << 2,
    "mode_capture": 0b1 << 3,
    "enroll_session": 0b1 << 4,
    "enroll_image": 0b1 << 5,
    "match": 0b1 << 6,
    "reset_sensor": 0b1 << 7,
    "sensor_maintenance": 0b1 << 8,
    "dont_change": 0b1 << 31,
}

fp_capture_types = {
    "vendor_format": 0,
    "defect_pxl_test": 1,
    "abnormal_test": 2,
    "noise_test": 3,
    "simple_image": 4,
    "pattern0": 8,
    "pattern1": 12,
    "quality_test": 16,
    "reset_test": 20,
}


def cmd_fp_mode(args, comm) -> int:
    """Sets the FP mode."""
    mode = 0
    if args.raw_mode:
        mode = args.raw_mode
    else:
        capture_type = 0
        for arg in args.mode:
            mode |= fp_modes.get(arg, 0)
            capture_type = fp_capture_types.get(arg, capture_type)
        mode |= capture_type << 26
    print("Mode to set: " + hex(mode))
    mode_ec = commands.FpModeCmd0(mode)
    ret = mode_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to set FP mode: {ret.name}")
        return ret
    print("Mode set to: " + hex(mode_ec.response.mode))
    return ret


def cmd_fp_info(_args, comm) -> int:
    """Prints FP info."""
    fp_info_cmd = commands.get_cmd(commands.ECCommandsIds.FP_INFO, comm)
    if not fp_info_cmd:
        print("No supported FP info")
        return -1
    fp_info = fp_info_cmd()
    ret = fp_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to get FP info: {ret.name}")
        return ret

    if fp_info.cmd_version == 1:
        print("Vendor ID: " + hex(fp_info.response.vendor_id))
        print("Product ID: " + hex(fp_info.response.product_id))
        print("Model ID: " + hex(fp_info.response.model_id))
        print("Version: " + hex(fp_info.response.version))
        print("Frame size: " + str(fp_info.response.frame_size))
        print("Pixel format: " + hex(fp_info.response.pixel_format))
        print("Width: " + str(fp_info.response.width))
        print("Height: " + str(fp_info.response.height))
        print("BPP: " + str(fp_info.response.bpp))
        print("Error: " + hex(fp_info.response.errors))
        print("Template size: " + str(fp_info.response.template_size))
        print("Template max: " + str(fp_info.response.template_max))
        print("Template valid: " + str(fp_info.response.template_valid))
        print("Template dirty: " + hex(fp_info.response.template_dirty))
        print("Template version: " + hex(fp_info.response.template_version))
    elif fp_info.cmd_version == 2:
        print("Vendor ID: " + hex(fp_info.response.vendor_id))
        print("Product ID: " + hex(fp_info.response.product_id))
        print("Model ID: " + hex(fp_info.response.model_id))
        print("Version: " + hex(fp_info.response.version))
        print(
            "Number of capture types: "
            + str(fp_info.response.num_capture_types)
        )
        print("Errors: " + hex(fp_info.response.errors))
        print("Template size: " + str(fp_info.response.template_size))
        print("Template max: " + str(fp_info.response.template_max))
        print("Template valid: " + str(fp_info.response.template_valid))
        print("Template dirty: " + hex(fp_info.response.template_dirty))
        print("Template version: " + hex(fp_info.response.template_version))
        for i, image_frame_params in enumerate(
            fp_info.response.image_frame_params
        ):
            print("Image frame params nr: " + str(i))
            print("\tFrame size: " + str(image_frame_params.frame_size))
            print("\tPixel format: " + hex(image_frame_params.pixel_format))
            print("\tWidth: " + str(image_frame_params.width))
            print("\tHeight: " + str(image_frame_params.height))
            print("\tBPP: " + str(image_frame_params.bpp))
            print("\tCapture type: " + str(image_frame_params.fp_capture_type))
    return ret


def cmd_fp_vendor(args, comm) -> int:
    """Prints vendor specific data."""
    vendor_ec = commands.FpVendorCmd0(args.param1)
    ret = vendor_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to retrieve vendor specific data: {ret.name}")
    elif not vendor_ec.response or not vendor_ec.response.payload:
        print("Empty vendor specific data")
    else:
        print(f"Vendor specific data size = {len(vendor_ec.response.payload)}")
        print(vendor_ec.response.payload)
    return ret


def cmd_enter_bootloader(_args, comm) -> int:
    """Enters the bootloader."""
    enter_bootloader = commands.EnterBootloaderCmd0()
    return enter_bootloader.run(comm)


def flash_read_to_file(file: str, offset: int, size: int, comm) -> int:
    """Reads flash to a file."""
    read_bytes = 0
    ret = 0
    # Get max response size
    pr_info = commands.ProtocolInfoCmd0()
    ret = pr_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret
    max_res_size = (
        pr_info.response.max_response_packet_size - command.RESPONSE_HEADER_LEN
    )
    with open(file, "wb") as out_file:
        while read_bytes < size:
            remaining_bytes = size - read_bytes
            if remaining_bytes > max_res_size:
                chunk = max_res_size
            else:
                chunk = remaining_bytes

            flash_read = commands.FlashReadCmd0(
                offset=offset + read_bytes, size=chunk
            )
            ret = flash_read.run(comm)
            if ret != commands.EcCommandResult.SUCCESS:
                return ret
            out_file.write(flash_read.response.data)
            read_bytes += chunk

    return ret


def cmd_flash_read(args, comm) -> int:
    """Reads flash to a file."""
    return flash_read_to_file(args.file, args.offset, args.size, comm)


def flash_write_from_file(file: str, offset: int, comm) -> int:
    """Writes flash from a file."""

    # Get max request size
    fl_info = commands.FlashInfoCmd2(0)
    ret = fl_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret
    max_req_size = fl_info.response.write_ideal_size

    with open(file, "rb") as in_file:
        data = in_file.read()

    ret = 0
    size = len(data)
    written_bytes = 0

    while written_bytes < size:

        remaining_bytes = size - written_bytes
        if remaining_bytes > max_req_size:
            chunk = max_req_size
        else:
            chunk = remaining_bytes

        flash_write = commands.FlashWriteCmd0(
            offset=offset + written_bytes,
            data=data[written_bytes : written_bytes + chunk],
        )
        ret = flash_write.run(comm)
        if ret != commands.EcCommandResult.SUCCESS:
            return ret
        written_bytes += chunk

    return ret


def cmd_flash_write(args, comm) -> int:
    """Writes flash from a file."""
    return flash_write_from_file(args.file, offset=args.offset, comm=comm)


def cmd_flash_erase(args, comm) -> int:
    """Erases flash."""
    flash_erase = commands.FlashEraseCmd0(offset=args.offset, size=args.size)
    return flash_erase.run(comm)


def cmd_flash_region_info(args, comm) -> int:
    """Prints flash region info."""
    flash_region_info = commands.FlashRegionInfoCmd1(region=args.region)
    ret = flash_region_info.run(comm)

    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Error getting flash region info: {ret.name}")
        return ret

    print("Offset: " + hex(flash_region_info.response.offset))
    print("Size: " + hex(flash_region_info.response.size))

    return ret


def cmd_reboot_ec(args, comm) -> int:
    """Reboots the EC."""
    reboot_ec = commands.RebootECCmd0(cmd=args.cmd)
    return reboot_ec.run(comm)


def _jump_to_ro_and_verify(comm) -> int:
    """Jumps to RO and verifies."""
    reboot_ec = commands.RebootECCmd0(cmd=commands.ECRebootCmd.JUMP_RO)
    ret = reboot_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to jump to RO")
        return ret

    time.sleep(2)
    comm.connect()
    get_ver = commands.GetVersionCmd1()
    ret = get_ver.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to get version after jump to RO")
        return ret

    if get_ver.response.curr_image != commands.ImageType.RO:
        print("Failed to stay in RO")
        return -1

    print("Stayed in RO after sysjump")
    return commands.EcCommandResult.SUCCESS


def _backup_rw_partition(comm) -> tuple[int, str, int, int]:
    """Backs up the RW partition."""
    flash_region_info = commands.FlashRegionInfoCmd1(
        region=commands.FlashRegion.UPDATE
    )
    ret = flash_region_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to get RW info")
        return ret, None, None, None

    offset = flash_region_info.response.offset
    size = flash_region_info.response.size
    rw_file = "rw_tmp.bin"
    ret = flash_read_to_file(
        rw_file,
        offset,
        size,
        comm,
    )
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to read RW")
        return ret, None, None, None

    print(f"RW stored in {rw_file}")
    return commands.EcCommandResult.SUCCESS, rw_file, offset, size


def _erase_rw_partition(comm, offset, size) -> int:
    """Erases the RW partition."""
    erase_offset = offset
    while erase_offset < offset + size:
        chunk = 1024 * 8
        flash_erase = commands.FlashEraseCmd0(offset=erase_offset, size=chunk)
        ret = flash_erase.run(comm)
        # In progress
        if ret == commands.EcCommandResult.IN_PROGRESS:
            time.sleep(0.1)
        elif ret != commands.EcCommandResult.SUCCESS:
            print(f"Failed to flash erase: {erase_offset:x}")
            return ret

        erase_offset += chunk

    print("RW erased")
    return commands.EcCommandResult.SUCCESS


def _restore_rw_partition(comm, rw_file, offset) -> int:
    """Restores the RW partition."""
    reboot_ec = commands.RebootECCmd0(cmd=commands.ECRebootCmd.COLD)
    ret = reboot_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to reboot after erase")
        return ret

    time.sleep(3)
    comm.connect()
    get_ver = commands.GetVersionCmd1()
    ret = get_ver.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to get version after reboot")
        return ret

    if get_ver.response.curr_image != commands.ImageType.RO:
        print("Failed to stay in RO after erase")
        return -1

    print("Stayed in RO after reboot")
    ret = flash_write_from_file(rw_file, offset, comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to re-write RW")
        return ret
    print("RW re-written")
    return commands.EcCommandResult.SUCCESS


def _reboot_and_verify_rw(comm) -> int:
    """Reboots and verifies the RW image."""
    reboot_ec = commands.RebootECCmd0(cmd=commands.ECRebootCmd.COLD)
    ret = reboot_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to reboot after RW write")
        return ret

    time.sleep(3)
    comm.connect()
    get_ver = commands.GetVersionCmd1()
    ret = get_ver.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print("Failed to get version after RW write")
        return ret

    if get_ver.response.curr_image != commands.ImageType.RW:
        print("Failed to jump to RW")
        return -1

    print("Done")
    return commands.EcCommandResult.SUCCESS


def cmd_reflash_rw(_args, comm) -> int:
    """Reflashes the RW partition."""
    ret = _jump_to_ro_and_verify(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret

    ret, rw_file, offset, size = _backup_rw_partition(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret

    ret = _erase_rw_partition(comm, offset, size)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret

    ret = _restore_rw_partition(comm, rw_file, offset)
    if ret != commands.EcCommandResult.SUCCESS:
        return ret

    return _reboot_and_verify_rw(comm)


def auto_int(x) -> int:
    """Converts a string to an int, automatically detecting the base."""
    return int(x, 0)


def add_subcommand(subparsers, name, help_text, func, args):
    """Adds a subcommand to the subparsers object."""
    sub_parser = subparsers.add_parser(name, help=help_text)
    sub_parser.set_defaults(func=func)
    for arg_name, arg_params in args.items():
        sub_parser.add_argument(arg_name, **arg_params)


subcommands = {
    "version": {"help": "Get version", "func": cmd_get_version},
    "flinfo": {
        "help": "Flash info",
        "func": cmd_fl_info,
        "args": {"num_banks_desc": {"type": auto_int}},
    },
    "prinfo": {"help": "Protocol info", "func": cmd_pr_info},
    "fpmode": {
        "help": "FP mode",
        "func": cmd_fp_mode,
        "args": {
            "--raw_mode": {"type": auto_int},
            "mode": {
                "type": str,
                "nargs": "*",
                "choices": list(fp_modes.keys())
                + list(fp_capture_types.keys()),
            },
        },
    },
    "fpinfo": {"help": "FP info", "func": cmd_fp_info},
    "fpvendor": {
        "help": "Vendor specific command",
        "func": cmd_fp_vendor,
        "args": {"param1": {"type": auto_int}},
    },
    "flashread": {
        "help": "Flash read",
        "func": cmd_flash_read,
        "args": {
            "offset": {"type": auto_int},
            "size": {"type": auto_int},
            "file": {"type": str},
        },
    },
    "flashwrite": {
        "help": "Flash write",
        "func": cmd_flash_write,
        "args": {
            "offset": {"type": auto_int},
            "file": {"type": str},
        },
    },
    "flasherase": {
        "help": "Flash erase",
        "func": cmd_flash_erase,
        "args": {
            "offset": {"type": auto_int},
            "size": {"type": auto_int},
        },
    },
    "flashregioninfo": {
        "help": "Flash region info",
        "func": cmd_flash_region_info,
        "args": {
            "region": {
                "type": commands.FlashRegion.from_string,
                "choices": list(commands.FlashRegion),
                "help": "Region",
            }
        },
    },
    "bootloader": {"help": "Enter bootloader", "func": cmd_enter_bootloader},
    "reboot_ec": {
        "help": "Reboot EC",
        "func": cmd_reboot_ec,
        "args": {
            "cmd": {
                "type": commands.ECRebootCmd.from_string,
                "choices": list(commands.ECRebootCmd),
                "help": "Reboot command",
            }
        },
    },
    "reflash_rw": {"help": "Try reflashing rw", "func": cmd_reflash_rw},
}


def add_subcommands(subparsers):
    """Adds subparsers objects for supported commands."""

    for name, command_info in subcommands.items():
        add_subcommand(
            subparsers,
            name,
            command_info["help"],
            command_info["func"],
            command_info.get("args", {}),
        )


def main():
    """Main function."""
    parser = argparse.ArgumentParser(
        description="USB version of ectool",
    )

    subparsers = parser.add_subparsers(
        dest="command", help="Host command to run", required=True
    )

    add_subcommands(subparsers)

    args = parser.parse_args()

    try:
        with communication.UsbCommunication() as comm:
            return args.func(args, comm)
    except communication.UsbCommunicationError as e:
        print(f"{e}")
        return -1


if __name__ == "__main__":
    sys.exit(main())
