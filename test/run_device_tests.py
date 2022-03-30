#!/usr/bin/env python3

# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=line-too-long
"""Runs unit tests on device and displays the results.

This script assumes you have a ~/.servodrc config file with a line that
corresponds to the board being tested.

See https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servo.md#servodrc
"""
# pylint: enable=line-too-long

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
from typing import Optional, BinaryIO, List

# pylint: disable=import-error
import colorama  # type: ignore[import]
import fmap
# pylint: enable=import-error

EC_DIR = Path(os.path.dirname(os.path.realpath(__file__))).parent
JTRACE_FLASH_SCRIPT = os.path.join(EC_DIR, 'util/flash_jlink.py')
SERVO_MICRO_FLASH_SCRIPT = os.path.join(EC_DIR, 'util/flash_ec')

ALL_TESTS_PASSED_REGEX = re.compile(r'Pass!\r\n')
ALL_TESTS_FAILED_REGEX = re.compile(r'Fail! \(\d+ tests\)\r\n')

SINGLE_CHECK_PASSED_REGEX = re.compile(r'Pass: .*')
SINGLE_CHECK_FAILED_REGEX = re.compile(r'.*failed:.*')

ASSERTION_FAILURE_REGEX = re.compile(r'ASSERTION FAILURE.*')

DATA_ACCESS_VIOLATION_8020000_REGEX = re.compile(
    r'Data access violation, mfar = 8020000\r\n')
DATA_ACCESS_VIOLATION_8040000_REGEX = re.compile(
    r'Data access violation, mfar = 8040000\r\n')
DATA_ACCESS_VIOLATION_80C0000_REGEX = re.compile(
    r'Data access violation, mfar = 80c0000\r\n')
DATA_ACCESS_VIOLATION_80E0000_REGEX = re.compile(
    r'Data access violation, mfar = 80e0000\r\n')
DATA_ACCESS_VIOLATION_20000000_REGEX = re.compile(
    r'Data access violation, mfar = 20000000\r\n')
DATA_ACCESS_VIOLATION_24000000_REGEX = re.compile(
    r'Data access violation, mfar = 24000000\r\n')

BLOONCHIPPER = 'bloonchipper'
DARTMONKEY = 'dartmonkey'

JTRACE = 'jtrace'
SERVO_MICRO = 'servo_micro'

GCC = 'gcc'
CLANG = 'clang'


class ImageType(Enum):
    """EC Image type to use for the test."""
    RO = 1
    RW = 2


class BoardConfig:
    """Board-specific configuration."""

    def __init__(self, name, servo_uart_name, servo_power_enable,
                 rollback_region0_regex, rollback_region1_regex, mpu_regex):
        self.name = name
        self.servo_uart_name = servo_uart_name
        self.servo_power_enable = servo_power_enable
        self.rollback_region0_regex = rollback_region0_regex
        self.rollback_region1_regex = rollback_region1_regex
        self.mpu_regex = mpu_regex


class TestConfig:
    """Configuration for a given test."""

    def __init__(self, name, image_to_use=ImageType.RW, finish_regexes=None,
                 fail_regexes=None, toggle_power=False, test_args=None,
                 num_flash_attempts=2, timeout_secs=10,
                 enable_hw_write_protect=False, ro_image=None,
                 build_board=None):
        if test_args is None:
            test_args = []
        if finish_regexes is None:
            finish_regexes = [ALL_TESTS_PASSED_REGEX, ALL_TESTS_FAILED_REGEX]
        if fail_regexes is None:
            fail_regexes = [SINGLE_CHECK_FAILED_REGEX, ALL_TESTS_FAILED_REGEX,
                            ASSERTION_FAILURE_REGEX]

        self.name = name
        self.image_to_use = image_to_use
        self.finish_regexes = finish_regexes
        self.fail_regexes = fail_regexes
        self.test_args = test_args
        self.toggle_power = toggle_power
        self.num_flash_attempts = num_flash_attempts
        self.timeout_secs = timeout_secs
        self.enable_hw_write_protect = enable_hw_write_protect
        self.logs = []
        self.passed = False
        self.num_fails = 0
        self.num_passes = 0
        self.ro_image = ro_image
        self.build_board = build_board


# All possible tests.
class AllTests:
    """All possible tests."""

    @staticmethod
    def get(board_config: BoardConfig):
        tests = {
            'aes':
                TestConfig(name='aes'),
            'cec':
                TestConfig(name='cec'),
            'cortexm_fpu':
                TestConfig(name='cortexm_fpu'),
            'crc':
                TestConfig(name='crc'),
            'flash_physical':
                TestConfig(name='flash_physical', image_to_use=ImageType.RO,
                           toggle_power=True),
            'flash_write_protect':
                TestConfig(name='flash_write_protect',
                           image_to_use=ImageType.RO,
                           toggle_power=True, enable_hw_write_protect=True),
            'fpsensor_hw':
                TestConfig(name='fpsensor_hw'),
            'fpsensor_spi_ro':
                TestConfig(name='fpsensor', image_to_use=ImageType.RO,
                           test_args=['spi']),
            'fpsensor_spi_rw':
                TestConfig(name='fpsensor', test_args=['spi']),
            'fpsensor_uart_ro':
                TestConfig(name='fpsensor', image_to_use=ImageType.RO,
                           test_args=['uart']),
            'fpsensor_uart_rw':
                TestConfig(name='fpsensor', test_args=['uart']),
            'mpu_ro':
                TestConfig(name='mpu',
                           image_to_use=ImageType.RO,
                           finish_regexes=[board_config.mpu_regex]),
            'mpu_rw':
                TestConfig(name='mpu',
                           finish_regexes=[board_config.mpu_regex]),
            'mutex':
                TestConfig(name='mutex'),
            'pingpong':
                TestConfig(name='pingpong'),
            'printf':
                TestConfig(name='printf'),
            'queue':
                TestConfig(name='queue'),
            'rollback_region0':
                TestConfig(name='rollback', finish_regexes=[
                    board_config.rollback_region0_regex],
                           test_args=['region0']),
            'rollback_region1':
                TestConfig(name='rollback', finish_regexes=[
                    board_config.rollback_region1_regex],
                           test_args=['region1']),
            'rollback_entropy':
                TestConfig(name='rollback_entropy', image_to_use=ImageType.RO),
            'rtc':
                TestConfig(name='rtc'),
            'sha256':
                TestConfig(name='sha256'),
            'sha256_unrolled':
                TestConfig(name='sha256_unrolled'),
            'static_if':
                TestConfig(name='static_if'),
            'system_is_locked_wp_on':
                TestConfig(name='system_is_locked', test_args=['wp_on'],
                           toggle_power=True, enable_hw_write_protect=True),
            'system_is_locked_wp_off':
                TestConfig(name='system_is_locked', test_args=['wp_off'],
                           toggle_power=True, enable_hw_write_protect=False),
            'timer_dos':
                TestConfig(name='timer_dos'),
            'utils':
                TestConfig(name='utils', timeout_secs=20),
            'utils_str':
                TestConfig(name='utils_str'),
        }

        if board_config.name == BLOONCHIPPER:
            tests['stm32f_rtc'] = TestConfig(name='stm32f_rtc')

        return tests


BLOONCHIPPER_CONFIG = BoardConfig(
    name=BLOONCHIPPER,
    servo_uart_name='raw_fpmcu_console_uart_pty',
    servo_power_enable='fpmcu_pp3300',
    rollback_region0_regex=DATA_ACCESS_VIOLATION_8020000_REGEX,
    rollback_region1_regex=DATA_ACCESS_VIOLATION_8040000_REGEX,
    mpu_regex=DATA_ACCESS_VIOLATION_20000000_REGEX,
)

DARTMONKEY_CONFIG = BoardConfig(
    name=DARTMONKEY,
    servo_uart_name='raw_fpmcu_console_uart_pty',
    servo_power_enable='fpmcu_pp3300',
    rollback_region0_regex=DATA_ACCESS_VIOLATION_80C0000_REGEX,
    rollback_region1_regex=DATA_ACCESS_VIOLATION_80E0000_REGEX,
    mpu_regex=DATA_ACCESS_VIOLATION_24000000_REGEX,
)

BOARD_CONFIGS = {
    'bloonchipper': BLOONCHIPPER_CONFIG,
    'dartmonkey': DARTMONKEY_CONFIG,
}


def read_file_gsutil(path: str) -> bytes:
    """Get data from bucket, using gsutil tool"""
    cmd = ['gsutil', 'cat', path]

    logging.debug('Running command: "%s"', ' '.join(cmd))
    gsutil = subprocess.run(cmd, stdout=subprocess.PIPE)  # pylint: disable=subprocess-run-check
    gsutil.check_returncode()

    return gsutil.stdout


def find_section_offset_size(section: str, image: bytes) -> (int, int):
    """Get offset and size of the section in image"""
    areas = fmap.fmap_decode(image)['areas']
    area = next(area for area in areas if area['name'] == section)
    return area['offset'], area['size']


def read_section(src: bytes, section: str) -> bytes:
    """Read FMAP section content into byte array"""
    (src_start, src_size) = find_section_offset_size(section, src)
    src_end = src_start + src_size
    return src[src_start:src_end]


def write_section(data: bytes, image: bytearray, section: str):
    """Replace the specified section in image with the contents of data"""
    (section_start, section_size) = find_section_offset_size(section, image)

    if section_size < len(data):
        raise ValueError(section + ' section size is not enough to store data')

    section_end = section_start + section_size
    filling = bytes([0xff for _ in range(section_size - len(data))])

    image[section_start:section_end] = data + filling


def copy_section(src: bytes, dst: bytearray, section: str):
    """Copy section from src image to dst image"""
    (src_start, src_size) = find_section_offset_size(section, src)
    (dst_start, dst_size) = find_section_offset_size(section, dst)

    if dst_size < src_size:
        raise ValueError('Section ' + section + ' from source image has '
                         'greater size than the section in destination image')

    src_end = src_start + src_size
    dst_end = dst_start + dst_size
    filling = bytes([0xff for _ in range(dst_size - src_size)])

    dst[dst_start:dst_end] = src[src_start:src_end] + filling


def replace_ro(image: bytearray, ro: bytes):
    """Replace RO in image with provided one"""
    # Backup RO public key since its private part was used to sign RW.
    ro_pubkey = read_section(image, 'KEY_RO')

    # Copy RO part of the firmware to the image. Please note that RO public key
    # is copied too since EC_RO area includes KEY_RO area.
    copy_section(ro, image, 'EC_RO')

    # Restore RO public key.
    write_section(ro_pubkey, image, 'KEY_RO')


def get_console(board_config: BoardConfig) -> Optional[str]:
    """Get the name of the console for a given board."""
    cmd = [
        'dut-control',
        board_config.servo_uart_name,
    ]
    logging.debug('Running command: "%s"', ' '.join(cmd))

    with subprocess.Popen(cmd, stdout=subprocess.PIPE) as proc:
        for line in io.TextIOWrapper(proc.stdout):  # type: ignore[arg-type]
            logging.debug(line)
            pty = line.split(':')
            if len(pty) == 2 and pty[0] == board_config.servo_uart_name:
                return pty[1].strip()

    return None


def power(board_config: BoardConfig, on: bool) -> None:
    """Turn power to board on/off."""
    if on:
        state = 'pp3300'
    else:
        state = 'off'

    cmd = [
        'dut-control',
        board_config.servo_power_enable + ':' + state,
    ]
    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()  # pylint: disable=subprocess-run-check


def hw_write_protect(enable: bool) -> None:
    """Enable/disable hardware write protect."""
    if enable:
        state = 'force_on'
    else:
        state = 'force_off'

    cmd = [
        'dut-control',
        'fw_wp_state:' + state,
        ]
    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()  # pylint: disable=subprocess-run-check


def build(test_name: str, board_name: str, compiler: str) -> None:
    """Build specified test for specified board."""
    cmd = ['make']

    if compiler == CLANG:
        cmd = cmd + ['CC=arm-none-eabi-clang']

    cmd = cmd + [
        'BOARD=' + board_name,
        'test-' + test_name,
        '-j',
    ]

    logging.debug('Running command: "%s"', ' '.join(cmd))
    subprocess.run(cmd).check_returncode()  # pylint: disable=subprocess-run-check


def flash(image_path: str, board: str, flasher: str, remote: str) -> bool:
    """Flash specified test to specified board."""
    logging.info('Flashing test')

    cmd = []
    if flasher == JTRACE:
        cmd.append(JTRACE_FLASH_SCRIPT)
        if remote:
            cmd.extend(['--remote', remote])
    elif flasher == SERVO_MICRO:
        cmd.append(SERVO_MICRO_FLASH_SCRIPT)
    else:
        logging.error('Unknown flasher: "%s"', flasher)
        return False
    cmd.extend([
        '--board', board,
        '--image', image_path,
    ])
    logging.debug('Running command: "%s"', ' '.join(cmd))
    completed_process = subprocess.run(cmd)  # pylint: disable=subprocess-run-check
    return completed_process.returncode == 0


def patch_image(test: TestConfig, image_path: str):
    """Replace RO part of the firmware with provided one."""
    with open(image_path, 'rb+') as f:
        image = bytearray(f.read())
        ro = read_file_gsutil(test.ro_image)
        replace_ro(image, ro)
        f.seek(0)
        f.write(image)
        f.truncate()


def readline(executor: ThreadPoolExecutor, f: BinaryIO, timeout_secs: int) -> \
             Optional[bytes]:
    """Read a line with timeout."""
    a = executor.submit(f.readline)
    try:
        return a.result(timeout_secs)
    except concurrent.futures.TimeoutError:
        return None


def readlines_until_timeout(executor, f: BinaryIO, timeout_secs: int) -> \
                            List[bytes]:
    """Continuously read lines for timeout_secs."""
    lines: List[bytes] = []
    while True:
        line = readline(executor, f, timeout_secs)
        if not line:
            return lines
        lines.append(line)


def process_console_output_line(line: bytes, test: TestConfig):
    try:
        line_str = line.decode()

        if SINGLE_CHECK_PASSED_REGEX.match(line_str):
            test.num_passes += 1

        for regex in test.fail_regexes:
            if regex.match(line_str):
                test.num_fails += 1
                break

        return line_str
    except UnicodeDecodeError:
        # Sometimes we get non-unicode from the console (e.g., when the
        # board reboots.) Not much we can do in this case, so we'll just
        # ignore it.
        return None


def run_test(test: TestConfig, console: str, executor: ThreadPoolExecutor) ->\
             bool:
    """Run specified test."""
    start = time.time()
    with open(console, 'wb+', buffering=0) as c:
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
                if now - start > test.timeout_secs:
                    logging.debug('Test timed out')
                    return False
                continue

            logging.debug(line)
            test.logs.append(line)
            # Look for test_print_result() output (success or failure)
            line_str = process_console_output_line(line, test)
            if line_str is None:
                # Sometimes we get non-unicode from the console (e.g., when the
                # board reboots.) Not much we can do in this case, so we'll just
                # ignore it.
                continue

            for r in test.finish_regexes:
                if r.match(line_str):
                    # flush read the remaining
                    lines = readlines_until_timeout(executor, c, 1)
                    logging.debug(lines)
                    test.logs.append(lines)

                    for line in lines:
                        process_console_output_line(line, test)

                    return test.num_fails == 0


def get_test_list(config: BoardConfig, test_args) -> List[TestConfig]:
    """Get a list of tests to run."""
    if test_args == 'all':
        return list(AllTests.get(config).values())

    test_list = []
    for t in test_args:
        logging.debug('test: %s', t)
        test_regex = re.compile(t)
        tests = [v for k, v in AllTests.get(config).items()
                 if test_regex.fullmatch(k)]
        if not tests:
            logging.error('Unable to find test config for "%s"', t)
            sys.exit(1)
        test_list += tests

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

    flasher_choices = [SERVO_MICRO, JTRACE]
    parser.add_argument(
        '--flasher', '-f',
        choices=flasher_choices,
        default=JTRACE
    )

    compiler_options = [GCC, CLANG]
    parser.add_argument('--compiler', '-c',
                        choices=compiler_options,
                        default=GCC)

    # This might be expanded to serve as a "remote" for flash_ec also, so
    # we will leave it generic.
    parser.add_argument(
        '--remote', '-n',
        help='The remote host:ip to connect to J-Link. '
        'This is passed to flash_jlink.py.',
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
        build_board = args.board
        # If test provides this information, build image for board specified
        # by test.
        if test.build_board is not None:
            build_board = test.build_board

        # build test binary
        build(test.name, build_board, args.compiler)

        image_path = os.path.join(EC_DIR, 'build', build_board, test.name,
                                  test.name + '.bin')

        if test.ro_image is not None:
            try:
                patch_image(test, image_path)
            except Exception as exception:
                logging.warning('An exception occurred while patching '
                                'image: %s', exception)
                test.passed = False
                continue

        # flash test binary
        # TODO(b/158327221): First attempt to flash fails after
        #  flash_write_protect test is run; works after second attempt.
        flash_succeeded = False
        for i in range(0, test.num_flash_attempts):
            logging.debug('Flash attempt %d', i + 1)
            if flash(image_path, args.board, args.flasher, args.remote):
                flash_succeeded = True
                break
            time.sleep(1)

        if not flash_succeeded:
            logging.debug('Flashing failed after max attempts: %d',
                          test.num_flash_attempts)
            test.passed = False
            continue

        if test.toggle_power:
            power(board_config, on=False)
            time.sleep(1)
            power(board_config, on=True)

        hw_write_protect(test.enable_hw_write_protect)

        # run the test
        logging.info('Running test: "%s"', test.name)
        console = get_console(board_config)
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
