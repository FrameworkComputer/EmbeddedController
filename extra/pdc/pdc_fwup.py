# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Perform a Realtek PDC FW update by streaming new firmware over the EC console
interface from a host machine.
"""

import ast
import base64
import logging
from pathlib import Path
import struct
import time
from typing import List, Optional, Tuple
import xmlrpc.client


PDC_FW_OFFSET_VENDOR_VID_L = 0x1F89E
PDC_FW_OFFSET_VENDOR_VID_H = 0x1F89F
PDC_FW_OFFSET_VENDOR_PID_L = 0x1F8A0
PDC_FW_OFFSET_VENDOR_PID_H = 0x1F8A1

PDC_FW_OFFSET_PROJECT_NAME = 0x1FC00

PDC_FW_OFFSET_FW_VERSION_MAJOR = 0x7EF9
PDC_FW_OFFSET_FW_VERSION_MINOR = 0x7EFA
PDC_FW_OFFSET_FW_VERSION_CONFIG = 0x1F802

PDC_FW_OFFSET_PORT_USED_OFFSET = 0x1F805


def get_fw_binary_config(fw_pkg: bytearray) -> dict:
    """Extract FW config values from a RTK PDC FW binary"""

    port_used_lookup = {
        0x00: "PORTB",
        0x01: "PORTA",
        0x02: "Dual-port",
    }

    return {
        "proj_name": fw_pkg[
            PDC_FW_OFFSET_PROJECT_NAME : PDC_FW_OFFSET_PROJECT_NAME + 12
        ].decode("ascii"),
        "vid": (
            fw_pkg[PDC_FW_OFFSET_VENDOR_VID_H] << 8
            | fw_pkg[PDC_FW_OFFSET_VENDOR_VID_L]
        ),
        "pid": (
            fw_pkg[PDC_FW_OFFSET_VENDOR_PID_H] << 8
            | fw_pkg[PDC_FW_OFFSET_VENDOR_PID_L]
        ),
        "port_config": port_used_lookup.get(
            fw_pkg[PDC_FW_OFFSET_PORT_USED_OFFSET], "Unknown"
        ),
        "version": (
            fw_pkg[PDC_FW_OFFSET_FW_VERSION_MAJOR],
            fw_pkg[PDC_FW_OFFSET_FW_VERSION_MINOR],
            fw_pkg[PDC_FW_OFFSET_FW_VERSION_CONFIG],
        ),
    }


class ServodClient(xmlrpc.client.ServerProxy):
    """Interface with `servod` using the HTTP XML RPC interface

    This is significantly faster than calling dut-control as a subprocess since
    we don't need to start up a docker container each time. Programming the PDC
    requires several thousand console commands to be issued.
    """

    # Servod control names
    CONTROL_EC_UART_REGEXP = "ec_uart_regexp"
    CONTROL_EC_UART_CMD = "ec_uart_cmd"
    CONTROL_EC_UART_TIMEOUT = "ec_uart_timeout"

    def __init__(self, servod_host: str, servod_port: int):
        uri = f"http://{servod_host}:{servod_port}"

        super().__init__(uri)

        self.log = logging.getLogger()
        self.log.info("Connecting to servod at %s", uri)

    def _run_ec_command_get_output(
        self, cmd: str, regexp: List[str]
    ) -> List[str]:
        """Run an EC console command and return output matching the regexp"""

        if not regexp:
            raise ValueError("Need regular expressions to match on")

        try:
            # Servod expects the (escaped) string representation of a Python list
            self.set(ServodClient.CONTROL_EC_UART_REGEXP, str(regexp))

            self.set(ServodClient.CONTROL_EC_UART_CMD, cmd)

            return ast.literal_eval(self.get(ServodClient.CONTROL_EC_UART_CMD))
        finally:
            self.set(ServodClient.CONTROL_EC_UART_REGEXP, "None")

    def _run_ec_command(self, cmd: str) -> None:
        """Run an EC command but don't collect any output"""
        self.set(ServodClient.CONTROL_EC_UART_CMD, cmd)

    def get_pdc_fw_ver(
        self, port: int, live: Optional[bool] = True
    ) -> Tuple[Tuple[int, int, int], str]:
        """Get the currently running PDC FW version and project name"""

        output = self._run_ec_command_get_output(
            f"pdc info {port} {int(live)}",
            [
                "FW Ver: ([\\d]+).([\\d]+).([\\d]+)\r\n",
                "Project Name: '(.*)'\r\n",
            ],
        )

        # Return a ((major, minor, patch), project name) tuple
        return (
            (int(output[0][1]), int(output[0][2]), int(output[0][3])),
            output[1][1],
        )

    def fwup_start(self, port: int):
        """Start a FW update session"""

        self._run_ec_command_get_output(
            f"pdc_rtk_fwup start {port}", ["RTK_FWUP: Started"]
        )

    def fwup_write(self, data: bytes) -> int:
        """Transfer FW payload data to the EC via the console"""

        cmd = f"pdc_rtk_fwup write {base64.b64encode(data).decode('ascii')}"

        output = self._run_ec_command_get_output(
            cmd, ["RTK_FWUP: bytes written: (\\d+)\r\n"]
        )

        return int(output[0][1])

    def fwup_finish(self):
        """Finalize the update after all data is transferred"""

        orig_timeout = self.get(ServodClient.CONTROL_EC_UART_TIMEOUT)

        # Extend the UART timeout because the validation and restart delay
        # steps take a long time (~7s + 5s)
        self.set(ServodClient.CONTROL_EC_UART_TIMEOUT, 16.0)
        try:
            self._run_ec_command_get_output(
                "pdc_rtk_fwup finish", ["PDC FWUP successful"]
            )
        finally:
            self.set(ServodClient.CONTROL_EC_UART_TIMEOUT, orig_timeout)

    def fwup_abort(self):
        """Try to recover the PDC subsystem after a failed update procedure"""

        self._run_ec_command("pdc_rtk_fwup abort")


# Number of host FWUP packets to send at a time, per call to
# `pdc_rtk_fwup write`. This is impacted by the Zephyr shell buffer size.
# Overrunning this buffer on the EC console side will result in loss of data.
HOST_FWUP_PACKET_MAX_CHUNK_COUNT = 2

# Maximum number of FW payload bytes that can be included in each write packet,
# per the RTK ISP interface
RTK_FW_CHUNK_SIZE = 29

# Each RTS54xx flash bank is divided into 64KiB segments (2 per bank). Writes
# cannot cross segment boundaries, so be aware when sizing chunks.
RTK_FLASH_SEGMENT_SIZE = 64 * 1024  # 64 KiB

# Size of a PDC FW image
RTK_FLASH_BANK_SIZE = RTK_FLASH_SEGMENT_SIZE * 2

# Struct definition must match the EC's `struct host_fwup_packet` in
# `zephyr/drivers/usbc/pdc_rts54xx_fwup.c`
HOST_FWUP_PACKET_FMT = "<BHB29s"
assert struct.calcsize(HOST_FWUP_PACKET_FMT) == 33


def write_flash(fw_image: bytes, servo: ServodClient):
    """Write the supplied PDC FW binary to the EC console piece by piece"""

    assert len(fw_image) == (RTK_FLASH_BANK_SIZE)

    log = logging.getLogger()

    bytes_written = 0
    ec_bytes_received = 0

    progress_log_target = 4000

    log.info("Starting FW transfer")

    def get_next_chunk() -> Tuple[int, bytes]:
        # Return the next firmware chunk up to 29 bytes at a time. Write cannot
        # cross a flash segment boundary (64 KiB, 128 KiB)

        segment = 0 if bytes_written < RTK_FLASH_SEGMENT_SIZE else 1
        segment_boundary = RTK_FLASH_SEGMENT_SIZE * (segment + 1)
        chunk_len = min(
            RTK_FW_CHUNK_SIZE,
            min(segment_boundary, len(fw_image)) - bytes_written,
        )

        return segment, fw_image[bytes_written : bytes_written + chunk_len]

    while bytes_written < len(fw_image):
        packets = bytearray()

        for _ in range(HOST_FWUP_PACKET_MAX_CHUNK_COUNT):
            # Line up as many packets as possible. Sending multiple in one
            # console command invocation reduces overhead.
            seg, chunk = get_next_chunk()

            if len(chunk) == 0:
                # All done
                break

            # Build packet and append to the buffer for transmission
            packets.extend(
                struct.pack(
                    HOST_FWUP_PACKET_FMT,
                    seg,
                    bytes_written & 0xFFFF,
                    len(chunk),
                    chunk.ljust(RTK_FW_CHUNK_SIZE, b"\x00"),
                )
            )
            log.debug(
                "Packet: seg=%d, offset=%d, len=%d",
                seg,
                bytes_written & 0xFFFF,
                len(chunk),
            )

            bytes_written += len(chunk)

        log.debug(
            "Transmit: len=%d (%d packets), bytes_written=%d",
            len(packets),
            len(packets) / struct.calcsize(HOST_FWUP_PACKET_FMT),
            bytes_written,
        )

        # Send to console
        ec_bytes_received = servo.fwup_write(packets)

        assert (
            ec_bytes_received == bytes_written
        ), f"{ec_bytes_received=} vs {bytes_written=}"

        # Show progress info every 4KB transferred
        if ec_bytes_received > progress_log_target:
            log.info(
                "Progess: %d/%d bytes transferred (%.2f%%)",
                progress_log_target,
                RTK_FLASH_BANK_SIZE,
                100.0 * progress_log_target / RTK_FLASH_BANK_SIZE,
            )
            progress_log_target += 4000

    log.info(
        "FW transfer completed: %d/%d bytes received by EC",
        ec_bytes_received,
        RTK_FLASH_BANK_SIZE,
    )


def main(
    servod_host: str, servod_port: int, pdc_fw_path: Path, usbc_port: int
) -> int:
    """Process a PDC FW update"""

    log = logging.getLogger()

    with open(pdc_fw_path, "rb") as f:
        pdc_fw = f.read()

    if len(pdc_fw) != RTK_FLASH_BANK_SIZE:
        log.error(
            "Invalid PDC FW image size. Expected %d B, got %d",
            RTK_FLASH_BANK_SIZE,
            len(pdc_fw),
        )
        return 1

    pdc_fw_info = get_fw_binary_config(pdc_fw)
    log.info(
        "New FW: %d.%d.%d ('%s'), %04x:%04x, Port Config: %s",
        *pdc_fw_info["version"],
        pdc_fw_info["proj_name"],
        pdc_fw_info["vid"],
        pdc_fw_info["pid"],
        pdc_fw_info["port_config"],
    )

    servo = ServodClient(servod_host, servod_port)

    pdc_live_ver, pdc_live_proj_name = servo.get_pdc_fw_ver(usbc_port)
    log.info("Current FW: %d.%d.%d ('%s')", *pdc_live_ver, pdc_live_proj_name)

    try:
        # Start firmware update session
        log.info("Starting firmware update session (port C%d)", usbc_port)
        servo.fwup_start(usbc_port)

        # Stream FW through the console
        write_flash(pdc_fw, servo)

        # Finish firmware update session
        log.info("Finalizing firmware update. This may take a moment.")
        servo.fwup_finish()
        log.info("✅ Update succeeded")
    except:
        # In case of failure, try to recover the PDC subsystem
        log.info("❌ Update failed. Attempting to recover PDC.")

        servo.fwup_abort()
        raise

    # Check FW version again after update. Using a polling routine because the
    # PDC stack might still be re-initializing immediately after finishing the
    # update.
    for _ in range(3):
        time.sleep(2.0)
        try:
            pdc_live_ver, pdc_live_proj_name = servo.get_pdc_fw_ver(usbc_port)
            log.info(
                "Current FW: %d.%d.%d ('%s')", *pdc_live_ver, pdc_live_proj_name
            )
            break
        except xmlrpc.client.Fault:
            log.info("Waiting for PDC stack to restart...")

    return 0


if __name__ == "__main__":
    import argparse
    import sys

    logging.basicConfig(
        format="%(asctime)s %(levelname)-8s %(message)s",
        level=logging.INFO,
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    parser = argparse.ArgumentParser()
    parser.add_argument("pdc_fw_image", type=Path, help="Path to PDC FW binary")
    parser.add_argument(
        "--host", type=str, default="localhost", help="Servod hostname"
    )
    parser.add_argument("--port", type=int, default=9999, help="Servod port")
    parser.add_argument(
        "--usbc_port",
        "-c",
        type=int,
        default=0,
        help="USB-C port number on DUT to target",
    )

    args = parser.parse_args()

    sys.exit(main(args.host, args.port, args.pdc_fw_image, args.usbc_port))
