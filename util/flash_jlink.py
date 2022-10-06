#!/usr/bin/env python3

# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=logging-fstring-interpolation

"""Flashes firmware using Segger J-Link.

This script requires Segger hardware attached via JTAG/SWD.

See
https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint-debugging.md#flash
for instructions.
"""

import argparse
import logging
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

DEFAULT_SEGGER_REMOTE_PORT = 19020

# Commands are documented here: https://wiki.segger.com/J-Link_Commander
JLINK_COMMANDS = """
exitonerror 1
r
loadfile {FIRMWARE} {FLASH_ADDRESS}
r
go
exit
"""


class BoardConfig:
    """Board configuration."""

    def __init__(self, interface, device, flash_address):
        self.interface = interface
        self.device = device
        self.flash_address = flash_address


SWD_INTERFACE = "SWD"
STM32_DEFAULT_FLASH_ADDRESS = "0x8000000"
DRAGONCLAW_CONFIG = BoardConfig(
    interface=SWD_INTERFACE,
    device="STM32F412CG",
    flash_address=STM32_DEFAULT_FLASH_ADDRESS,
)
ICETOWER_CONFIG = BoardConfig(
    interface=SWD_INTERFACE,
    device="STM32H743ZI",
    flash_address=STM32_DEFAULT_FLASH_ADDRESS,
)

BOARD_CONFIGS = {
    "dragonclaw": DRAGONCLAW_CONFIG,
    "bloonchipper": DRAGONCLAW_CONFIG,
    "nucleo-f412zg": DRAGONCLAW_CONFIG,
    "dartmonkey": ICETOWER_CONFIG,
    "icetower": ICETOWER_CONFIG,
    "nucleo-dartmonkey": ICETOWER_CONFIG,
    "nucleo-h743zi": ICETOWER_CONFIG,
}


def is_tcp_port_open(host: str, tcp_port: int) -> bool:
    """Checks if the TCP host port is open."""

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2)  # 2 Second Timeout
    try:
        sock.connect((host, tcp_port))
        sock.shutdown(socket.SHUT_RDWR)
    except ConnectionRefusedError:
        return False
    except socket.timeout:
        return False
    finally:
        sock.close()
    # Other errors are propagated as odd exceptions.

    # We shutdown and closed the connection, but the server may need a second
    # to start listening again. If the following error is seen, this timeout
    # should be increased. 300ms seems to be the minimum.
    #
    # Connecting to J-Link via IP...FAILED: Can not connect to J-Link via \
    #   TCP/IP (127.0.0.1, port 19020)
    time.sleep(0.5)
    return True


def create_jlink_command_file(firmware_file, config):
    tmp = tempfile.NamedTemporaryFile()
    tmp.write(
        JLINK_COMMANDS.format(
            FIRMWARE=firmware_file, FLASH_ADDRESS=config.flash_address
        ).encode("utf-8")
    )
    tmp.flush()
    return tmp


def flash(jlink_exe, remote, device, interface, cmd_file):
    cmd = [
        jlink_exe,
    ]

    if remote:
        logging.debug(f"Connecting to J-Link over TCP/IP {remote}.")
        remote_components = remote.split(":")
        if len(remote_components) not in [1, 2]:
            logging.debug(f'Given remote "{remote}" is malformed.')
            return 1

        host = remote_components[0]
        try:
            ip = socket.gethostbyname(host)
        except socket.gaierror as e:
            logging.error(f'Failed to resolve host "{host}": {e}.')
            return 1
        logging.debug(f"Resolved {host} as {ip}.")
        port = DEFAULT_SEGGER_REMOTE_PORT

        if len(remote_components) == 2:
            try:
                port = int(remote_components[1])
            except ValueError:
                logging.error(
                    f'Given remote port "{remote_components[1]}" is malformed.'
                )
                return 1

        remote = f"{ip}:{port}"

        logging.debug(f"Checking connection to {remote}.")
        if not is_tcp_port_open(ip, port):
            logging.error(
                f"JLink server doesn't seem to be listening on {remote}."
            )
            logging.error("Ensure that JLinkRemoteServerCLExe is running.")
            return 1
        cmd.extend(["-ip", remote])

    cmd.extend(
        [
            "-device",
            device,
            "-if",
            interface,
            "-speed",
            "auto",
            "-autoconnect",
            "1",
            "-CommandFile",
            cmd_file,
        ]
    )
    logging.debug('Running command: "%s"', " ".join(cmd))
    completed_process = subprocess.run(
        cmd
    )  # pylint: disable=subprocess-run-check
    logging.debug("JLink return code: %d", completed_process.returncode)
    return completed_process.returncode


def main(argv: list):

    parser = argparse.ArgumentParser()

    default_jlink = "./JLink_Linux_V684a_x86_64/JLinkExe"
    if shutil.which(default_jlink) is None:
        default_jlink = "JLinkExe"
    parser.add_argument(
        "--jlink",
        "-j",
        help="JLinkExe path (default: " + default_jlink + ")",
        default=default_jlink,
    )

    parser.add_argument(
        "--remote",
        "-n",
        help="Use TCP/IP host[:port] to connect to a J-Link or "
        "JLinkRemoteServerCLExe. If unspecified, connect over USB.",
    )

    default_board = "bloonchipper"
    parser.add_argument(
        "--board",
        "-b",
        help="Board (default: " + default_board + ")",
        default=default_board,
    )

    default_firmware = os.path.join("./build", default_board, "ec.bin")
    parser.add_argument(
        "--image",
        "-i",
        help="Firmware binary (default: " + default_firmware + ")",
        default=default_firmware,
    )

    log_level_choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]
    parser.add_argument(
        "--log_level", "-l", choices=log_level_choices, default="DEBUG"
    )

    args = parser.parse_args(argv)
    logging.basicConfig(level=args.log_level)

    if args.board not in BOARD_CONFIGS:
        logging.error('Unable to find a config for board: "%s"', args.board)
        sys.exit(1)

    config = BOARD_CONFIGS[args.board]

    args.image = os.path.realpath(args.image)
    args.jlink = args.jlink

    cmd_file = create_jlink_command_file(args.image, config)
    ret_code = flash(
        args.jlink, args.remote, config.device, config.interface, cmd_file.name
    )
    cmd_file.close()
    return ret_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
