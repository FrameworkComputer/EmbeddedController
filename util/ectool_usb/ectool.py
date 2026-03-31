# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A USB version of ectool."""

import argparse
from functools import partial
import sys
import time

import command
import communication

# pylint: disable=import-error
# cryptography is not available in CROS SDK
import cryptography.exceptions
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric import utils

# pylint: enable=import-error
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
        capture_type = fp_capture_types["simple_image"]
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
    elif fp_info.cmd_version in (2, 3):
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
            if fp_info.cmd_version == 3:
                print(
                    "\tImage offset: "
                    + str(image_frame_params.image_data_offset_bytes)
                )
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


def get_max_res_size(comm) -> int:
    """Gets max response size"""
    pr_info = commands.ProtocolInfoCmd0()
    ret = pr_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        return -1
    return (
        pr_info.response.max_response_packet_size - command.RESPONSE_HEADER_LEN
    )


def flash_read_to_file(file: str, offset: int, size: int, comm) -> int:
    """Reads flash to a file."""
    read_bytes = 0
    ret = 0
    max_res_size = get_max_res_size(comm)
    if max_res_size < 0:
        return -1
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


def cmd_rwsig_action(args, comm) -> int:
    """Perform RWSIG action."""
    rwsig_action = commands.RwSigActionCmd0(action=args.action)
    ret = rwsig_action.run(comm)

    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to perform RWSIG action: {ret.name}")

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


def verify_raw(raw_public_key, raw_sig, data):
    """Helper to verify raw signatures (converts back to DER for the library)."""

    public_key = ec.EllipticCurvePublicKey.from_encoded_point(
        ec.SECP256R1(), raw_public_key
    )

    r = int.from_bytes(raw_sig[:32], byteorder="big")
    s = int.from_bytes(raw_sig[32:], byteorder="big")
    der_sig = utils.encode_dss_signature(r, s)

    public_key.verify(der_sig, data, ec.ECDSA(hashes.SHA256()))


def cmd_fp_ascp_claim(args, comm) -> int:
    """Gets ASCP claim and verifies it."""

    claim = commands.FpAscpClaimCmd0()
    ret = claim.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to claim: {ret.name}")
        return ret
    print(f"pk_m: [{claim.response.pk_m.hex()}]")
    print(f"s_goog: [{claim.response.s_goog.hex()}]")
    print(f"pk_d: [{claim.response.pk_d.hex()}]")
    print(f"s_m: [{claim.response.s_m.hex()}]")
    print(f"pk_f: [{claim.response.pk_f.hex()}]")
    print(f"h_f: [{claim.response.h_f.hex()}]")
    print(f"s_d: [{claim.response.s_d.hex()}]")

    try:
        with open(args.pk_goog_file, "rb") as f:
            pk_goog = f.read()
            print("Checking s_goog...")
            verify_raw(pk_goog, claim.response.s_goog, claim.response.pk_m)
            print("s_goog is valid")
            print("Checking s_m...")
            verify_raw(
                claim.response.pk_m, claim.response.s_m, claim.response.pk_d
            )
            print("s_m is valid")
            print("Checking s_d...")
            verify_raw(
                claim.response.pk_d,
                claim.response.s_d,
                bytes([0xC0, 0x01]) + claim.response.h_f + claim.response.pk_f,
            )
            print("s_d is valid.\n")
    except FileNotFoundError as e:
        print(f"File not found: {e}")
    except ValueError as e:
        print(f"Wrong param: {e}")
    except cryptography.exceptions.InvalidSignature:
        print("Invalid signature.")

    return ret


def cmd_fp_ascp_establish(args, comm) -> int:
    """Establishes ASCP."""
    try:
        with open(args.pk_g_file, "rb") as f:
            pk_g = f.read()
            establish = commands.FpAscpEstablishCmd0(pk_g)
            ret = establish.run(comm)
            if ret != commands.EcCommandResult.SUCCESS:
                print(f"Failed to establish: {ret.name}")
            return ret
    except FileNotFoundError as e:
        print(f"File not found: {e}")
        return -1


events = {
    "key_matrix": 0,
    "host_event": 1,
    "sensor_fifo": 2,
    "button": 3,
    "switch": 4,
    "fingerprint": 5,
    "sysrq": 6,
    "host_event64": 7,
    "cec_event": 8,
    "cec_message": 9,
    "dp_alt_mode_entered": 10,
    "online_calibration": 11,
    "pchg": 12,
}


def receive_event(event_type: int, comm) -> bool:
    """Receives MKBP events until event type received or no more events."""
    get_next_event_cmd = commands.get_cmd(
        commands.ECCommandsIds.GET_NEXT_EVENT, comm
    )
    if not get_next_event_cmd:
        print("No supported get next event")
        return False
    while True:
        get_next_event = get_next_event_cmd()
        ret = get_next_event.run(comm)
        if ret != commands.EcCommandResult.SUCCESS:
            if ret == commands.EcCommandResult.UNAVAILABLE:
                print("No events available")
            else:
                print(f"Failed to get next event: {ret.name}")
            return False
        if get_next_event.response.event_type & 0x7F == event_type:
            print("Event type: " + hex(get_next_event.response.event_type))
            print("Event data: " + str(get_next_event.response.event_data))
            return True


def cmd_wait_for_event(args, comm) -> int:
    """Waits for MKBP event."""

    if not receive_event(events[args.type], comm):
        start_time = time.perf_counter()
        remaining_time = args.timeout
        while remaining_time > 0:
            print(f"Wait for event {remaining_time}s")
            if comm.wait_for_event(int(1000 * remaining_time)):
                if receive_event(events[args.type], comm):
                    return 0
            remaining_time = args.timeout - (time.perf_counter() - start_time)
        print(f"No event in {args.timeout}s")
        return -1
    return 0


def get_frame_size(capture_type, comm) -> tuple[int, int]:
    """Gets FP frame size."""

    fp_info = commands.FpInfoCmd2()
    ret = fp_info.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        print(f"Failed to get FP info: {ret.name}")
        return None

    for image_frame_params in fp_info.response.image_frame_params:
        if image_frame_params.fp_capture_type == capture_type:
            return image_frame_params.width, image_frame_params.height

    print(f"No matching capture type: {capture_type}")
    return None


def cmd_fp_frame(args, comm) -> int:
    """Captures FP frame."""

    capture_type = fp_capture_types[args.type or "simple_image"]
    width, height = get_frame_size(capture_type, comm)
    frame_size = width * height
    max_res_size = get_max_res_size(comm)
    if max_res_size < 0:
        print("Failed to get max command size")
        return -1
    raw_data = bytes()

    fp_frame_cmd = commands.get_cmd(commands.ECCommandsIds.FP_FRAME, comm)
    if not fp_frame_cmd:
        print("No supported FP frame")
        return -1

    if fp_frame_cmd == commands.FpFrameCmd1:
        fp_frame_cmd = partial(fp_frame_cmd, 0, 0)
    else:
        fp_frame_cmd = partial(fp_frame_cmd, 0)

    offset = 0
    while offset < frame_size:
        chunk_size = min(frame_size - offset, max_res_size)
        fp_frame = fp_frame_cmd(offset, chunk_size)
        ret = fp_frame.run(comm)
        if ret != commands.EcCommandResult.SUCCESS:
            print(f"Failed to get FP frame: {ret.name}")
            return ret
        offset += chunk_size
        raw_data += fp_frame.response.data

    header = f"P5\n{width} {height}\n255\n"

    with open(args.file, "wb") as f:
        f.write(header.encode("ascii"))
        f.write(raw_data)

    print(f"{len(raw_data)} bytes written to {args.file}")
    return 0


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
    "rwsig_action": {
        "help": "RWSIG action",
        "func": cmd_rwsig_action,
        "args": {
            "action": {
                "type": commands.RwSigAction.from_string,
                "choices": list(commands.RwSigAction),
                "help": "RWSIG action",
            }
        },
    },
    "fpascp": {
        "help": "Get ASCP claim and verify it",
        "func": cmd_fp_ascp_claim,
        "args": {
            "pk_goog_file": {"type": str},
        },
    },
    "fpascp_establish": {
        "help": "Establish ASCP",
        "func": cmd_fp_ascp_establish,
        "args": {
            "pk_g_file": {"type": str},
        },
    },
    "wait_for_event": {
        "help": "Wait for the next MKBP event",
        "func": cmd_wait_for_event,
        "args": {
            "type": {"type": str, "choices": list(events.keys())},
            "timeout": {"type": auto_int, "help": "timeout in seconds"},
        },
    },
    "fpframe": {
        "help": "Capture FP frame",
        "func": cmd_fp_frame,
        "args": {
            "file": {"type": str},
            "type": {
                "type": str,
                "choices": list(fp_capture_types.keys()),
                "nargs": "?",
            },
        },
    },
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
