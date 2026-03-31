# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for EC host command definitions."""

from enum import IntEnum

from command import HostCommand


class EcCommandResult(IntEnum):
    """EC command result codes."""

    SUCCESS = 0
    INVALID_COMMAND = 1
    ERROR = 2
    INVALID_PARAM = 3
    ACCESS_DENIED = 4
    INVALID_RESPONSE = 5
    INVALID_VERSION = 6
    INVALID_CHECKSUM = 7
    IN_PROGRESS = 8
    UNAVAILABLE = 9
    TIMEOUT = 10
    OVERFLOW = 11
    INVALID_HEADER = 12
    REQUEST_TRUNCATED = 13
    RESPONSE_TOO_BIG = 14
    BUS_ERROR = 15
    BUSY = 16
    INVALID_HEADER_VERSION = 17
    INVALID_HEADER_CRC = 18
    INVALID_DATA_CRC = 19
    DUP_UNAVAILABLE = 20
    UNKNOWN = -1


class ECCommandsIds(IntEnum):
    """IDs of supported EC commands"""

    GET_VERSION = 0x0002
    GET_VERSIONS = 0x0008
    PROTOCOL_INFO = 0x000B
    FLASH_INFO = 0x0010
    FLASH_READ = 0x0011
    FLASH_WRITE = 0x0012
    FLASH_ERASE = 0x0013
    FLASH_REGION_INFO = 0x0016
    GET_NEXT_EVENT = 0x0067
    REBOOT_EC = 0x00D2
    ENTER_BOOTLOADER = 0x00E2
    RWSIG_ACTION = 0x011D
    FP_MODE = 0x0402
    FP_INFO = 0x0403
    FP_FRAME = 0x0404
    FP_VENDOR = 0x040B
    FP_ASCP_CLAIM = 0x0420
    FP_ASCP_ESTABLISH = 0x0421


class ImageType(IntEnum):
    """EC Image type"""

    RO = 1
    RW = 2

    @staticmethod
    def get_image_name(image: int) -> str:
        """Returns the name of the image."""
        if image == ImageType.RO:
            return "RO"
        if image == ImageType.RW:
            return "RW"
        return "Unknown"


class ECRebootCmd(IntEnum):
    """EC Reboot commands."""

    CANCEL = 0
    JUMP_RO = 1
    JUMP_RW = 2
    COLD = 4
    DISABLE_JUMP = 5
    HIBERNATE = 6
    HIBERNATE_CLEAR_AP_OFF = 7
    COLD_AP_OFF = 8
    NO_OP = 9

    def __str__(self) -> str:
        return self.name

    @staticmethod
    def from_string(s) -> "ECRebootCmd":
        """Returns reboot command from a string."""
        return ECRebootCmd[s]


class FlashRegion(IntEnum):
    """Flash regions."""

    RO = 0
    ACTIVE = 1
    WP_RO = 2
    UPDATE = 3

    def __str__(self) -> str:
        """Returns the name of the region."""
        return self.name

    @staticmethod
    def from_string(s) -> "FlashRegion":
        """Returns the region from a string."""
        return FlashRegion[s]


class RwSigAction(IntEnum):
    """RwSig actions."""

    ABORT = 0
    CONTINUE = 1

    def __str__(self) -> str:
        """Returns the name of the RwSig action."""
        return self.name

    @staticmethod
    def from_string(s) -> "RwSigAction":
        """Returns the RwSigAction from a string."""
        return RwSigAction[s]


class ECCommand(HostCommand):
    """Base class for EC Commands."""

    def run(self, comm) -> EcCommandResult:
        """Runs the EC command."""
        ret = super().run(comm)
        try:
            return EcCommandResult(ret)
        except ValueError:
            print(f"Unknown error code {ret} returned by EC.")
            return EcCommandResult.UNKNOWN


class GetVersionCmd1(ECCommand):
    """Gets the version of the EC."""

    def __init__(self):
        # 32 bytes of RO string version
        # 32 bytes of RW string version
        # 32 bytes of fwid_ro
        # 4 bytes of current image
        # 32 bytes of fwid_rw
        response_msg = [
            ("ro_ver", "32s"),
            ("rw_ver", "32s"),
            ("fwid_ro", "32s"),
            ("curr_image", "I"),
            ("fwid_rw", "32s"),
        ]
        super().__init__(
            ECCommandsIds.GET_VERSION, 1, response_msg=response_msg
        )


class GetVersionsCmd1(ECCommand):
    """Gets the supported versions of a command."""

    def __init__(self, command):
        # 4 bytes of version mask
        response_msg = [("version_mask", "I")]
        # 2 bytes of cmd
        request_msg = [(command, "H")]
        super().__init__(
            ECCommandsIds.GET_VERSIONS,
            1,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class FlashInfoCmd2(ECCommand):
    """Gets flash information."""

    def __init__(self, num_banks_desc: int):
        # 4 bytes of flash_size
        # 4 bytes of flags
        # 4 bytes of write_ideal_size
        # 2 bytes of num_banks_total
        # 2 bytes of num_banks_desc
        # num_banks_desc of ec_flash_bank struct
        response_msg = [
            ("flash_size", "I"),
            ("flags", "I"),
            ("write_ideal_size", "I"),
            ("num_banks_total", "H"),
            ("num_banks_desc", "H"),
            ("ec_flash_banks", ""),
        ]
        # 2 bytes of num_banks
        # 2 bytes reserved
        request_msg = [(num_banks_desc, "H"), (0, "H")]
        # 2 bytes of count
        # 1 byte of size_exp
        # 1 byte of write_size_exp
        # 1 byte of erase_size_exp
        # 1 byte of protect_size_exp
        # 2 bytes of reserved
        ec_flash_bank = [
            ("count", "H"),
            ("size_exp", "B"),
            ("write_size_exp", "B"),
            ("erase_size_exp", "B"),
            ("protect_size_exp", "B"),
            ("reserved", "H"),
        ]
        super().__init__(
            ECCommandsIds.FLASH_INFO,
            2,
            response_msg=response_msg,
            request_msg=request_msg,
            variable_payload_msg=ec_flash_bank,
        )


class ProtocolInfoCmd0(ECCommand):
    """Gets protocol information."""

    def __init__(self):
        # 4 bytes of protocol version
        # 2 bytes of max_request_packet_size
        # 2 bytes of max_response_packet_size
        # 4 bytes of flags
        response_msg = [
            ("protocol_versions", "I"),
            ("max_request_packet_size", "H"),
            ("max_response_packet_size", "H"),
            ("flags", "I"),
        ]
        super().__init__(
            ECCommandsIds.PROTOCOL_INFO, 0, response_msg=response_msg
        )


class FpModeCmd0(ECCommand):
    """Sets the FP mode."""

    def __init__(self, mode: int):
        # 4 bytes of mode
        response_msg = [("mode", "I")]
        # 4 bytes of param1
        request_msg = [(mode, "I")]
        super().__init__(
            0x0402, 0, response_msg=response_msg, request_msg=request_msg
        )


class FpInfoCmd1(ECCommand):
    """Gets FP information (version 1)."""

    def __init__(self):
        # 4 bytes of vendor id
        # 4 bytes of product id
        # 4 bytes of model id
        # 4 bytes of version
        # 4 bytes of frame size
        # 4 bytes of pixel format
        # 2 bytes of width
        # 2 bytes of height
        # 2 bytes of bpp
        # 2 bytes of errors
        # 4 bytes of template size
        # 2 bytes of template max
        # 2 bytes of template valid
        # 4 bytes of template dirty
        # 4 bytes of template version
        response_msg = [
            ("vendor_id", "I"),
            ("product_id", "I"),
            ("model_id", "I"),
            ("version", "I"),
            ("frame_size", "I"),
            ("pixel_format", "I"),
            ("width", "H"),
            ("height", "H"),
            ("bpp", "H"),
            ("errors", "H"),
            ("template_size", "I"),
            ("template_max", "H"),
            ("template_valid", "H"),
            ("template_dirty", "I"),
            ("template_version", "I"),
        ]
        super().__init__(ECCommandsIds.FP_INFO, 1, response_msg=response_msg)


class FpInfoCmd2(ECCommand):
    """Gets FP information (version 2)."""

    def __init__(self):
        # 4 bytes of vendor id
        # 4 bytes of product id
        # 4 bytes of model id
        # 4 bytes of version
        # 2 bytes of num of capture types
        # 2 bytes of errors
        # 4 bytes of template size
        # 2 bytes of template max
        # 2 bytes of template valid
        # 4 bytes of template dirty
        # 4 bytes of template version
        # unknown number of image_frame
        response_msg = [
            ("vendor_id", "I"),
            ("product_id", "I"),
            ("model_id", "I"),
            ("version", "I"),
            ("num_capture_types", "H"),
            ("errors", "H"),
            ("template_size", "I"),
            ("template_max", "H"),
            ("template_valid", "H"),
            ("template_dirty", "I"),
            ("template_version", "I"),
            ("image_frame_params", ""),
        ]
        # fp_image_frame_params
        # 4 bytes of frame_size;
        # 4 bytes of pixel_format;
        # 2 bytes of width;
        # 2 bytes of height;
        # 2 bytes of bpp;
        # 1 byte of fp_capture_type;
        # 1 byte of reserved;
        image_frame = [
            ("frame_size", "I"),
            ("pixel_format", "I"),
            ("width", "H"),
            ("height", "H"),
            ("bpp", "H"),
            ("fp_capture_type", "B"),
            ("reserved", "B"),
        ]
        super().__init__(
            ECCommandsIds.FP_INFO,
            2,
            response_msg=response_msg,
            variable_payload_msg=image_frame,
        )


class FpInfoCmd3(ECCommand):
    """Gets FP information (version 3)."""

    def __init__(self):
        # 4 bytes of vendor id
        # 4 bytes of product id
        # 4 bytes of model id
        # 4 bytes of version
        # 2 bytes of num of capture types
        # 2 bytes of errors
        # 4 bytes of template size
        # 2 bytes of template max
        # 2 bytes of template valid
        # 4 bytes of template dirty
        # 4 bytes of template version
        # unknown number of image_frame
        response_msg = [
            ("vendor_id", "I"),
            ("product_id", "I"),
            ("model_id", "I"),
            ("version", "I"),
            ("num_capture_types", "H"),
            ("errors", "H"),
            ("template_size", "I"),
            ("template_max", "H"),
            ("template_valid", "H"),
            ("template_dirty", "I"),
            ("template_version", "I"),
            ("image_frame_params", ""),
        ]
        # fp_image_frame_params
        # 4 bytes of frame_size;
        # 4 bytes of image offset;
        # 4 bytes of pixel_format;
        # 2 bytes of width;
        # 2 bytes of height;
        # 2 bytes of bpp;
        # 1 byte of fp_capture_type;
        # 1 byte of reserved;
        image_frame = [
            ("frame_size", "I"),
            ("image_data_offset_bytes", "I"),
            ("pixel_format", "I"),
            ("width", "H"),
            ("height", "H"),
            ("bpp", "H"),
            ("fp_capture_type", "B"),
            ("reserved", "B"),
        ]
        super().__init__(
            ECCommandsIds.FP_INFO,
            3,
            response_msg=response_msg,
            variable_payload_msg=image_frame,
        )


class FpFrameCmd0(ECCommand):
    """Gets FP frame (version 0)."""

    def __init__(self, idx: int, offset: int, size: int):
        # Variable number of bytes in response
        response_msg = [("data", "")]
        # 4 bytes of offset with index
        # 4 bytes of size
        idx_off = ((idx << 28) | (offset & 0x0FFFFFFF)) & 0xFFFFFFFF
        request_msg = [
            (idx_off, "I"),
            (size, "I"),
        ]
        super().__init__(
            ECCommandsIds.FP_FRAME,
            0,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class FpFrameCmd1(ECCommand):
    """Gets FP frame (version 1)."""

    def __init__(self, cmd: int, idx: int, offset: int, size: int):
        # Variable number of bytes in response
        response_msg = [("data", "")]
        # 4 bytes of offset with index
        # 4 bytes of size
        request_msg = [
            (cmd, "B"),
            (0, "B"),
            (idx, "H"),
            (offset, "I"),
            (size, "I"),
        ]
        super().__init__(
            ECCommandsIds.FP_FRAME,
            1,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class FpVendorCmd0(ECCommand):
    """FP vendor command."""

    def __init__(self, param1: int):
        # Variable number of bytes in response
        response_msg = [("payload", "")]
        # 4 bytes of param1
        request_msg = [(param1, "I")]
        super().__init__(
            ECCommandsIds.FP_VENDOR,
            0,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class EnterBootloaderCmd0(ECCommand):
    """Enters the bootloader."""

    def __init__(self):
        super().__init__(ECCommandsIds.ENTER_BOOTLOADER, 0)


class FlashReadCmd0(ECCommand):
    """Reads from flash."""

    def __init__(self, offset: int, size: int):
        # "size" bytes of data
        response_msg = [("data", f"{size}s")]
        # 4 bytes of offset
        # 4 bytes of size
        request_msg = [(offset, "I"), (size, "I")]
        super().__init__(
            ECCommandsIds.FLASH_READ,
            0,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class FlashWriteCmd0(ECCommand):
    """Writes to flash."""

    def __init__(self, offset: int, data: bytes):
        size = len(data)
        # 4 bytes of offset
        # 4 bytes of size
        request_msg = [(offset, "I"), (size, "I"), (data, f"{size}s")]

        super().__init__(ECCommandsIds.FLASH_WRITE, 0, request_msg=request_msg)


class FlashEraseCmd0(ECCommand):
    """Erases flash."""

    def __init__(self, offset: int, size: int):
        # 4 bytes of offset
        # 4 bytes of size
        request_msg = [(offset, "I"), (size, "I")]

        super().__init__(ECCommandsIds.FLASH_ERASE, 0, request_msg=request_msg)


class FlashRegionInfoCmd1(ECCommand):
    """Gets flash region information."""

    def __init__(self, region: int):
        # 4 bytes of offset
        # 4 bytes of size
        response_msg = [("offset", "I"), ("size", "I")]
        # 4 bytes of region
        request_msg = [(region, "I")]
        super().__init__(
            ECCommandsIds.FLASH_REGION_INFO,
            1,
            response_msg=response_msg,
            request_msg=request_msg,
        )


class GetNextEventCmd2(ECCommand):
    """Gets next MKBP event."""

    def __init__(self):
        # 1 byte of event_type
        # 16 bytes of event_data
        response_msg = [("event_type", "B"), ("event_data", "16s")]
        super().__init__(
            ECCommandsIds.GET_NEXT_EVENT, 2, response_msg=response_msg
        )


class GetNextEventCmd3(ECCommand):
    """Gets next MKBP event."""

    def __init__(self):
        # 1 byte of event_type
        # Up to 18 bytes of event_data
        response_msg = [("event_type", "B"), ("event_data", "")]
        super().__init__(
            ECCommandsIds.GET_NEXT_EVENT, 3, response_msg=response_msg
        )


class RebootECCmd0(ECCommand):
    """Reboots the EC."""

    def __init__(self, cmd: int, flags: int = 0):
        # 1 byte of command
        # 1 byte of flags
        request_msg = [(cmd, "B"), (flags, "B")]

        super().__init__(ECCommandsIds.REBOOT_EC, 0, request_msg=request_msg)


class RwSigActionCmd0(ECCommand):
    """RWSIG action."""

    def __init__(self, action: int):
        # 4 bytes of action
        request_msg = [(action, "I")]

        super().__init__(ECCommandsIds.RWSIG_ACTION, 0, request_msg=request_msg)


class FpAscpClaimCmd0(ECCommand):
    """Gets ASCP claim."""

    def __init__(self):
        response_msg = [
            ("pk_m", "65s"),
            ("s_goog", "64s"),
            ("pk_d", "65s"),
            ("s_m", "64s"),
            ("pk_f", "65s"),
            ("h_f", "32s"),
            ("s_d", "64s"),
        ]
        super().__init__(
            ECCommandsIds.FP_ASCP_CLAIM, 0, response_msg=response_msg
        )


class FpAscpEstablishCmd0(ECCommand):
    """Establishes ASCP session."""

    def __init__(self, pk_g: bytearray):
        request_msg = [(pk_g, "65s")]
        super().__init__(
            ECCommandsIds.FP_ASCP_ESTABLISH, 0, request_msg=request_msg
        )


VERSIONED_COMMANDS = {
    ECCommandsIds.GET_VERSION: {1: GetVersionCmd1},
    ECCommandsIds.GET_VERSIONS: {1: GetVersionsCmd1},
    ECCommandsIds.PROTOCOL_INFO: {0: ProtocolInfoCmd0},
    ECCommandsIds.FLASH_INFO: {2: FlashInfoCmd2},
    ECCommandsIds.FLASH_READ: {0: FlashReadCmd0},
    ECCommandsIds.FLASH_WRITE: {0: FlashWriteCmd0},
    ECCommandsIds.FLASH_ERASE: {0: FlashEraseCmd0},
    ECCommandsIds.FLASH_REGION_INFO: {1: FlashRegionInfoCmd1},
    ECCommandsIds.GET_NEXT_EVENT: {2: GetNextEventCmd2, 3: GetNextEventCmd3},
    ECCommandsIds.REBOOT_EC: {0: RebootECCmd0},
    ECCommandsIds.ENTER_BOOTLOADER: {0: EnterBootloaderCmd0},
    ECCommandsIds.FP_MODE: {0: FpModeCmd0},
    ECCommandsIds.FP_INFO: {1: FpInfoCmd1, 2: FpInfoCmd2, 3: FpInfoCmd3},
    ECCommandsIds.FP_FRAME: {0: FpFrameCmd0, 1: FpFrameCmd1},
    ECCommandsIds.FP_VENDOR: {0: FpVendorCmd0},
    ECCommandsIds.RWSIG_ACTION: {0: RwSigActionCmd0},
    ECCommandsIds.FP_ASCP_CLAIM: {0: FpAscpClaimCmd0},
    ECCommandsIds.FP_ASCP_ESTABLISH: {0: FpAscpEstablishCmd0},
}


def get_versions(command_id, comm) -> list[int]:
    """Returns a list of supported versions for a given command."""
    get_ver = GetVersionsCmd1(command_id)
    ret = get_ver.run(comm)
    if ret != EcCommandResult.SUCCESS:
        return []
    versions = []
    version_mask = get_ver.response.version_mask
    i = 0
    while version_mask > 0:
        if version_mask & 1:
            versions.append(i)
        version_mask >>= 1
        i += 1
    return versions


def get_cmd(command_id: ECCommandsIds, comm) -> HostCommand:
    """Returns the highest supported command by both the device and the library."""
    versions = get_versions(command_id, comm)
    if len(versions) > 0:
        for version in sorted(versions, reverse=True):
            if version in VERSIONED_COMMANDS[command_id]:
                return VERSIONED_COMMANDS[command_id][version]
    return None
