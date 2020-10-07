#!/usr/bin/env python3

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Flashes firmware using Segger J-Link.

This script requires Segger hardware attached via JTAG/SWD.
"""

import argparse
import logging
import os
import shutil
import subprocess
import sys
import tempfile

# Commands are documented here: https://wiki.segger.com/J-Link_Commander
JLINK_COMMANDS = '''
exitonerror 1
r
loadfile {FIRMWARE}
r
go
exit
'''


class BoardConfig:
    def __init__(self, interface, device):
        self.interface = interface
        self.device = device


SWD_INTERFACE = 'SWD'
DRAGONCLAW_CONFIG = BoardConfig(interface=SWD_INTERFACE, device='STM32F412CG')
ICETOWER_CONFIG = BoardConfig(interface=SWD_INTERFACE, device='STM32H743ZI')

BOARD_CONFIGS = {
    'dragonclaw': DRAGONCLAW_CONFIG,
    'bloonchipper': DRAGONCLAW_CONFIG,
    'dartmonkey': ICETOWER_CONFIG,
    'icetower': ICETOWER_CONFIG,
}


def create_jlink_command_file(firmware_file):
    tmp = tempfile.NamedTemporaryFile()
    tmp.write(JLINK_COMMANDS.format(FIRMWARE=firmware_file).encode('utf-8'))
    tmp.flush()
    return tmp


def flash(jlink_exe, ip, device, interface, cmd_file):
    cmd = [
        jlink_exe,
    ]

    if len(ip) > 0:
        cmd.extend(['-ip', ip])

    cmd.extend([
        '-device', device,
        '-if', interface,
        '-speed', 'auto',
        '-autoconnect', '1',
        '-CommandFile', cmd_file,
        ])
    logging.debug('Running command: "%s"', ' '.join(cmd))
    completed_process = subprocess.run(cmd)
    logging.debug('JLink return code: %d', completed_process.returncode)
    return completed_process.returncode


def main(argv: list):

    parser = argparse.ArgumentParser()

    default_jlink = './JLink_Linux_V684a_x86_64/JLinkExe'
    if shutil.which(default_jlink) is None:
        default_jlink = 'JLinkExe'
    parser.add_argument(
        '--jlink', '-j',
        help='JLinkExe path (default: ' + default_jlink + ')',
        default=default_jlink)

    default_ip = '127.0.0.1:2551'
    parser.add_argument(
        '--ip', '-n',
        help='IP address of J-Link or machine running JLinkRemoteServerCLExe '
             '(default: ' + default_ip + ')',
        default=default_ip)

    default_board = 'bloonchipper'
    parser.add_argument(
        '--board', '-b',
        help='Board (default: ' + default_board + ')',
        default=default_board)

    default_firmware = os.path.join('./build', default_board, 'ec.bin')
    parser.add_argument(
        '--image', '-i',
        help='Firmware binary (default: ' + default_firmware + ')',
        default=default_firmware)

    log_level_choices = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
    parser.add_argument(
        '--log_level', '-l',
        choices=log_level_choices,
        default='DEBUG'
    )

    args = parser.parse_args(argv)
    logging.basicConfig(level=args.log_level)

    if args.board not in BOARD_CONFIGS:
        logging.error('Unable to find a config for board: "%s"', args.board)
        sys.exit(1)

    config = BOARD_CONFIGS[args.board]

    args.image = os.path.realpath(args.image)
    args.jlink = args.jlink

    cmd_file = create_jlink_command_file(args.image)
    ret_code = flash(args.jlink, args.ip, config.device, config.interface,
                     cmd_file.name)
    cmd_file.close()
    return ret_code


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
