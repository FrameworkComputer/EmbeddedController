#!/usr/bin/env python

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs unit tests on device and displays the results.

This script assumes you have a ~/.servodrc config file with a line that
corresponds to the board being tested.

See https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servo.md#servodrc
"""
import argparse
import concurrent
import io
import logging
import os
import re
import subprocess
import sys
import time
from concurrent.futures.thread import ThreadPoolExecutor
from enum import Enum
from pathlib import Path
import colorama

EC_DIR = Path(os.path.dirname(os.path.realpath(__file__))).parent
FLASH_SCRIPT = os.path.join(EC_DIR, 'util/flash_jlink.py')

ALL_TESTS_PASSED_REGEX = re.compile(r'Pass!\r\n')
ALL_TESTS_FAILED_REGEX = re.compile(r'Fail! \(\d+ tests\)\r\n')

SINGLE_CHECK_PASSED_REGEX = re.compile(r'Pass: .*')
SINGLE_CHECK_FAILED_REGEX = re.compile(r'.*failed:.*')

DATA_ACCESS_VIOLATION_8020000_REGEX = re.compile(
    r'Data access violation, mfar = 8020000\r\n')
DATA_ACCESS_VIOLATION_8040000_REGEX = re.compile(
    r'Data access violation, mfar = 8040000\r\n')
DATA_ACCESS_VIOLATION_20000000_REGEX = re.compile(
    r'Data access violation, mfar = 20000000\r\n')


class ImageType(Enum):
    """EC Image type to use for the test."""
    RO = 1
    RW = 2


class BoardConfig:
    """Board-specific configuration."""
    def __init__(self, test_list, servo_uart_name, servo_power_enable):
        self.test_list = test_list
        self.servo_uart_name = servo_uart_name
        self.servo_power_enable = servo_power_enable


class TestConfig:
    """Configuration for a given test."""

    def __init__(self, name, image_to_use=ImageType.RW, finish_regexes=None,
                 toggle_power=False, test_args=None):
        if test_args is None:
            test_args = []
        if finish_regexes is None:
            finish_regexes = [ALL_TESTS_PASSED_REGEX, ALL_TESTS_FAILED_REGEX]

        self.name = name
        self.image_to_use = image_to_use
        self.finish_regexes = finish_regexes
        self.test_args = test_args
        self.toggle_power = toggle_power
        self.logs = []
        self.passed = False
        self.num_fails = 0
        self.num_passes = 0


# All possible tests.
ALL_TESTS = {
    'aes':
        TestConfig(name='aes'),
    'crc32':
        TestConfig(name='crc32'),
    'flash_physical':
        TestConfig(name='flash_physical', image_to_use=ImageType.RO,
                   toggle_power=True),
    'flash_write_protect':
        TestConfig(name='flash_write_protect', image_to_use=ImageType.RO,
                   toggle_power=True),
    'mpu_ro':
        TestConfig(name='mpu',
                   image_to_use=ImageType.RO,
                   finish_regexes=[DATA_ACCESS_VIOLATION_20000000_REGEX]),
    'mpu_rw':
        TestConfig(name='mpu',
                   finish_regexes=[DATA_ACCESS_VIOLATION_20000000_REGEX]),
    'mutex':
        TestConfig(name='mutex'),
    'pingpong':
        TestConfig(name='pingpong'),
    'rollback_region0':
        TestConfig(name='rollback', finish_regexes=[
            DATA_ACCESS_VIOLATION_8020000_REGEX],
                   test_args=['region0']),
    'rollback_region1':
        TestConfig(name='rollback', finish_regexes=[
            DATA_ACCESS_VIOLATION_8040000_REGEX],
                   test_args=['region1']),
    'rollback_entropy':
        TestConfig(name='rollback_entropy', image_to_use=ImageType.RO),
    'rtc':
        TestConfig(name='rtc'),
    'sha256':
        TestConfig(name='sha256'),
    'sha256_unrolled':
        TestConfig(name='sha256_unrolled'),
    'stm32f_rtc':
        TestConfig(name='stm32f_rtc'),
}

BLOONCHIPPER_CONFIG = BoardConfig(
    test_list=ALL_TESTS.values(),
    servo_uart_name='raw_fpmcu_uart_pty',
    servo_power_enable='spi1_vref'
)
DARTMONKEY_CONFIG = BLOONCHIPPER_CONFIG

BOARD_CONFIGS = {
    'bloonchipper': BLOONCHIPPER_CONFIG,
    'dartmonkey': DARTMONKEY_CONFIG,
}


def get_console(board_name, board_config):
    """Get the name of the console for a given board."""
    cmd = [
        'dut-control',
        '-n', board_name,
        board_config.servo_uart_name,
    ]
    logging.debug('Running command: "%s"', ' '.join(cmd))

    with subprocess.Popen(cmd, stdout=subprocess.PIPE) as proc:
        for line in io.TextIOWrapper(proc.stdout):
            logging.debug(line)
            pty = line.split(':')
            if len(pty) == 2 and pty[0] == board_config.servo_uart_name:
                return pty[1].strip()

    return None


def power(board_name, board_config, on):
    """Turn power to board on/off."""
    if on:
        state = 'pp3300'
    else:
        state = 'off'

    cmd = [
        'dut-control',
        '-n', board_name,
        board_config.servo_power_enable + ':' + state,
    ]
    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()


def build(test_name, board_name):
    """Build specified test for specified board."""
    cmd = [
        'make',
        'BOARD=' + board_name,
        'test-' + test_name,
        '-j',
    ]

    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()


def flash(test_name, board):
    """Flash specified test to specified board."""
    logging.info("Flashing test")

    # TODO(b/151105339): Support ./util/flash_ec as well. It's slower, but only
    # requires servo micro.
    cmd = [
        FLASH_SCRIPT,
        '--board', board,
        '--image', os.path.join(EC_DIR, 'build', board, test_name,
                                test_name + '.bin'),
    ]
    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()


def readline(executor, f, timeout_secs):
    """Read a line with timeout."""
    a = executor.submit(f.readline)
    try:
        return a.result(timeout_secs)
    except concurrent.futures.TimeoutError:
        return None


def readlines_until_timeout(executor, f, timeout_secs):
    """Continuously read lines for timeout_secs."""
    lines = []
    while True:
        line = readline(executor, f, timeout_secs)
        if not line:
            return lines
        lines.append(line)


def run_test(test, console, executor, timeout_secs=10):
    """Run specified test."""
    start = time.time()
    with open(console, "wb+", buffering=0) as c:
        # Wait for boot to finish
        time.sleep(1)
        c.write('\n'.encode())
        if test.image_to_use == ImageType.RO:
            c.write('reboot ro\n'.encode())
            time.sleep(1)

        test_cmd = 'runtest ' + ' '.join(test.test_args) + '\n'
        c.write(test_cmd.encode())

        while True:
            c.flush()
            line = readline(executor, c, 1)
            if not line:
                now = time.time()
                if now - start > timeout_secs:
                    logging.debug("Test timed out")
                    return False
                continue

            logging.debug(line)
            test.logs.append(line)
            # Look for test_print_result() output (success or failure)
            try:
                line_str = line.decode()

                if SINGLE_CHECK_PASSED_REGEX.match(line_str):
                    test.num_passes += 1

                if SINGLE_CHECK_FAILED_REGEX.match(line_str):
                    test.num_fails += 1

                if ALL_TESTS_FAILED_REGEX.match(line_str):
                    test.num_fails += 1

                for r in test.finish_regexes:
                    if r.match(line_str):
                        # flush read the remaining
                        lines = readlines_until_timeout(executor, c, 1)
                        logging.debug(lines)
                        test.logs.append(lines)
                        return test.num_fails == 0

            except UnicodeDecodeError:
                # Sometimes we get non-unicode from the console (e.g., when the
                # board reboots.) Not much we can do in this case, so we'll just
                # ignore it.
                pass


def get_test_list(config, test_args):
    """Get a list of tests to run."""
    if test_args == 'all':
        return config.test_list

    test_list = []
    for t in test_args:
        logging.debug('test: %s', t)
        config = ALL_TESTS.get(t)
        if config is None:
            logging.error('Unable to find test config for "%s"', t)
            sys.exit(1)
        test_list.append(config)

    return test_list


def main():
    parser = argparse.ArgumentParser()

    default_board = 'bloonchipper'
    parser.add_argument(
        '--board', '-b',
        help='Board (default: ' + default_board + ')',
        default=default_board)

    default_tests = 'all'
    parser.add_argument(
        '--tests', '-t',
        nargs='+',
        help='Tests (default: ' + default_tests + ')',
        default=default_tests)

    log_level_choices = ['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL']
    parser.add_argument(
        '--log_level', '-l',
        choices=log_level_choices,
        default='DEBUG'
    )

    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)

    if args.board not in BOARD_CONFIGS:
        logging.error('Unable to find a config for board: "%s"', args.board)
        sys.exit(1)

    board_config = BOARD_CONFIGS[args.board]

    e = ThreadPoolExecutor(max_workers=1)

    test_list = get_test_list(board_config, args.tests)
    logging.debug('Running tests: %s', [t.name for t in test_list])

    for test in test_list:
        # build test binary
        build(test.name, args.board)

        # flash test binary
        flash(test.name, args.board)

        if test.toggle_power:
            power(args.board, board_config, on=False)
            time.sleep(1)
            power(args.board, board_config, on=True)

        # run the test
        logging.info('Running test: "%s"', test.name)
        console = get_console(args.board, board_config)
        test.passed = run_test(test, console, executor=e)

    colorama.init()
    exit_code = 0
    for test in test_list:
        # print results
        print('Test "' + test.name + '": ', end='')
        if test.passed:
            print(colorama.Fore.GREEN + 'PASSED')
        else:
            print(colorama.Fore.RED + 'FAILED')
            exit_code = 1

        print(colorama.Style.RESET_ALL)

    e.shutdown(wait=False)
    sys.exit(exit_code)


if __name__ == '__main__':
  sys.exit(main())
