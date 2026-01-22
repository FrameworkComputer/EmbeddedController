# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for host commands base."""

from collections import namedtuple
from enum import IntEnum
import struct
from typing import Any


# 1 byte - protocol version
# 1 byte - checksum
# 2 bytes - command id
# 1 byte - command version
# 1 byte - reserved
# 2 bytes - data length
REQUEST_HEADER_FMT = "<BBHBBH"
REQUEST_HEADER_LEN = struct.calcsize(REQUEST_HEADER_FMT)
# 1 byte - protocol version
# 1 byte - checksum
# 2 bytes - result
# 2 bytes - data length
# 2 bytes - reserved
RESPONSE_HEADER_FMT = "<BBHHH"
RESPONSE_HEADER_LEN = struct.calcsize(RESPONSE_HEADER_FMT)
ResponseHeaderType = namedtuple(
    "Header",
    [
        "protocol_version",
        "checksum",
        "result",
        "data_length",
        "reserved",
    ],
)


class HostCommandError(Exception):
    """Host command error."""


class HostCommandResult(IntEnum):
    """Host command results"""

    SUCCESS = 0
    UNKNOWN_ERROR = -1


class HostCommand:
    """Base class for Host Command."""

    cmd_version: int = 0
    _cmd_bytes: bytes
    _request_fmt: str = ""
    _response_fmt: str = ""
    _response_type: type = None
    _variable_response: bool = False
    _variable_payload_fmt: str = ""
    _variable_payload_type: type = None

    def __init__(
        self,
        cmd_id,
        cmd_version=0,
        request_msg: list[tuple[Any, str]] = None,
        response_msg: list[tuple[str, str]] = None,
        variable_payload_msg: list[tuple[str, str]] = None,
    ):
        """Initializes the host command."""
        self.cmd_version = cmd_version

        request_payload = b""
        if request_msg:
            _request_fmt = HostCommand._join_formats(request_msg)
            request_values = [value for value, _ in request_msg]
            request_payload = struct.pack(_request_fmt, *request_values)

        self._cmd_bytes = HostCommand.pack_command(
            cmd_id, cmd_version, request_payload
        )

        if response_msg:
            self._response_fmt = HostCommand._join_formats(response_msg)
            self._response_type = HostCommand._build_response(response_msg)
            self._variable_response = False
            # last field without known length means variable length response
            _, last_field_fmt = response_msg[-1]
            self._variable_response = last_field_fmt == ""

        if variable_payload_msg:
            self._variable_payload_fmt = HostCommand._join_formats(
                variable_payload_msg
            )
            self._variable_payload_type = HostCommand._build_response(
                variable_payload_msg
            )

        self.response = None

    @staticmethod
    def update_checksum(cmd: bytes) -> bytes:
        """Updates the checksum of the command."""
        # Make sure current checksum is 0
        assert len(cmd) >= REQUEST_HEADER_LEN, "Too Short request"
        cmd_list = list(cmd)
        cmd_list[1] = 0
        checksum = 0
        for x in cmd_list:
            checksum += x

        cmd_list[1] = (256 - checksum % 256) % 256

        return bytes(cmd_list)

    @staticmethod
    def checksum_valid(cmd: bytes) -> bool:
        """Checks if the checksum of the command is valid."""
        # Make sure current checksum is 0
        cmd_list = list(cmd)
        checksum = 0
        for x in cmd_list:
            checksum += x

        return checksum % 256 == 0

    @staticmethod
    def pack_command(cmd_id: int, cmd_ver: int, payload: bytes = b"") -> bytes:
        """Packs a command."""
        cmd_header = struct.pack(
            REQUEST_HEADER_FMT, 3, 0, cmd_id, cmd_ver, 0, len(payload)
        )

        cmd = cmd_header + payload
        cmd = HostCommand.update_checksum(cmd)

        return cmd

    @staticmethod
    def _join_formats(msg) -> str:
        """Joins formats to create format string."""
        return "<" + "".join(format for _, format in msg)

    @staticmethod
    def _build_response(msg) -> namedtuple:
        """Builds response namedtuple."""
        return namedtuple("Response", [name for name, _ in msg])

    def _send_request(self, comm) -> int:
        """Sends the command."""
        bytes_sent = comm.send(self._cmd_bytes)
        if bytes_sent != len(self._cmd_bytes):
            raise HostCommandError(f"Failed to send: {bytes_sent}")
        return bytes_sent

    def _receive_response(self, comm) -> tuple[bytes, ResponseHeaderType]:
        """Waits for and receives the response."""
        comm.wait()
        response_bytes = comm.receive()
        if len(response_bytes) < RESPONSE_HEADER_LEN:
            raise HostCommandError(
                f"Response shorter than header: {len(response_bytes)}"
            )
        try:
            response_header = ResponseHeaderType(
                *struct.unpack(
                    RESPONSE_HEADER_FMT, response_bytes[:RESPONSE_HEADER_LEN]
                )
            )
        except struct.error as e:
            raise HostCommandError(f"Failed to unpack header: {e}") from e

        remaining_bytes = response_header.data_length - (
            len(response_bytes) - RESPONSE_HEADER_LEN
        )
        if remaining_bytes > 0:
            response_bytes += comm.receive(remaining_bytes)
        return response_bytes, response_header

    def _validate_response(self, response_bytes, response_header) -> int:
        """Validates the response."""

        if not HostCommand.checksum_valid(response_bytes):
            raise HostCommandError("Response checksum invalid")

        if response_header.result != HostCommandResult.SUCCESS:
            return response_header.result

        expected_response_len = (
            struct.calcsize(self._response_fmt) + RESPONSE_HEADER_LEN
        )
        if self._variable_response:
            if expected_response_len > len(response_bytes):
                raise HostCommandError(
                    f"Variable response too short: {len(response_bytes)}, "
                    f"expected at least: {expected_response_len}"
                )
            expected_response_len = len(response_bytes)

        if expected_response_len != len(response_bytes):
            raise HostCommandError(
                f"Invalid response length. Received: {len(response_bytes)}, "
                f"expected: {expected_response_len}"
            )

        return HostCommandResult.SUCCESS

    def _adjust_response_fmt(self, response_len):
        if self._variable_response:
            remaining_variable_payload = (
                response_len
                - RESPONSE_HEADER_LEN
                - struct.calcsize(self._response_fmt)
            )
            self._response_fmt += f"{remaining_variable_payload}s"

    def _unpack_response(self, response_bytes):
        """Unpacks the response."""
        try:
            if self._response_type:
                response_unnamed = struct.unpack(
                    self._response_fmt, response_bytes[RESPONSE_HEADER_LEN:]
                )
                self.response = self._response_type(*response_unnamed)
        except struct.error as e:
            raise HostCommandError("Failed to unpack response payload") from e

    def _process_variable_payload(self):
        """Processes a variable-length payload."""
        if not self._variable_payload_type:
            return
        payload = self.response[-1]  # process last field
        element_len = struct.calcsize(self._variable_payload_fmt)
        element_nr = int(len(payload) / element_len)
        new_payload_field = [
            self._variable_payload_type(
                *struct.unpack_from(
                    self._variable_payload_fmt,
                    payload,
                    i * element_len,
                )
            )
            for i in range(element_nr)
        ]
        self.response = self.response._replace(
            **{self.response._fields[-1]: new_payload_field}
        )

    def send_cmd(self, comm) -> int:
        """Sends the command."""
        try:
            self._send_request(comm)
            response_bytes, response_header = self._receive_response(comm)

            valid = self._validate_response(response_bytes, response_header)
            if valid != HostCommandResult.SUCCESS:
                return valid

            self._adjust_response_fmt(len(response_bytes))

            self._unpack_response(response_bytes)

            self._process_variable_payload()

        except HostCommandError as e:
            print(f"ERROR: {e}")
            return HostCommandResult.UNKNOWN_ERROR
        except IOError as e:
            print(f"Communication error: {e}")
            return HostCommandResult.UNKNOWN_ERROR

        return HostCommandResult.SUCCESS

    def run(self, comm) -> int:
        """Runs the host command."""
        return self.send_cmd(comm)
